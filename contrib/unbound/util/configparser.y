/*
 * configparser.y -- yacc grammar for unbound configuration files
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
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

%{
#include "config.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "util/configyyrename.h"
#include "util/config_file.h"
#include "util/net_help.h"

int ub_c_lex(void);
void ub_c_error(const char *message);

static void validate_respip_action(const char* action);

/* these need to be global, otherwise they cannot be used inside yacc */
extern struct config_parser_state* cfg_parser;

#if 0
#define OUTYY(s)  printf s /* used ONLY when debugging */
#else
#define OUTYY(s)
#endif

%}
%union {
	char*	str;
};

%token SPACE LETTER NEWLINE COMMENT COLON ANY ZONESTR
%token <str> STRING_ARG
%token VAR_SERVER VAR_VERBOSITY VAR_NUM_THREADS VAR_PORT
%token VAR_OUTGOING_RANGE VAR_INTERFACE
%token VAR_DO_IP4 VAR_DO_IP6 VAR_PREFER_IP6 VAR_DO_UDP VAR_DO_TCP
%token VAR_TCP_MSS VAR_OUTGOING_TCP_MSS VAR_TCP_IDLE_TIMEOUT
%token VAR_EDNS_TCP_KEEPALIVE VAR_EDNS_TCP_KEEPALIVE_TIMEOUT
%token VAR_CHROOT VAR_USERNAME VAR_DIRECTORY VAR_LOGFILE VAR_PIDFILE
%token VAR_MSG_CACHE_SIZE VAR_MSG_CACHE_SLABS VAR_NUM_QUERIES_PER_THREAD
%token VAR_RRSET_CACHE_SIZE VAR_RRSET_CACHE_SLABS VAR_OUTGOING_NUM_TCP
%token VAR_INFRA_HOST_TTL VAR_INFRA_LAME_TTL VAR_INFRA_CACHE_SLABS
%token VAR_INFRA_CACHE_NUMHOSTS VAR_INFRA_CACHE_LAME_SIZE VAR_NAME
%token VAR_STUB_ZONE VAR_STUB_HOST VAR_STUB_ADDR VAR_TARGET_FETCH_POLICY
%token VAR_HARDEN_SHORT_BUFSIZE VAR_HARDEN_LARGE_QUERIES
%token VAR_FORWARD_ZONE VAR_FORWARD_HOST VAR_FORWARD_ADDR
%token VAR_DO_NOT_QUERY_ADDRESS VAR_HIDE_IDENTITY VAR_HIDE_VERSION
%token VAR_IDENTITY VAR_VERSION VAR_HARDEN_GLUE VAR_MODULE_CONF
%token VAR_TRUST_ANCHOR_FILE VAR_TRUST_ANCHOR VAR_VAL_OVERRIDE_DATE
%token VAR_BOGUS_TTL VAR_VAL_CLEAN_ADDITIONAL VAR_VAL_PERMISSIVE_MODE
%token VAR_INCOMING_NUM_TCP VAR_MSG_BUFFER_SIZE VAR_KEY_CACHE_SIZE
%token VAR_KEY_CACHE_SLABS VAR_TRUSTED_KEYS_FILE 
%token VAR_VAL_NSEC3_KEYSIZE_ITERATIONS VAR_USE_SYSLOG 
%token VAR_OUTGOING_INTERFACE VAR_ROOT_HINTS VAR_DO_NOT_QUERY_LOCALHOST
%token VAR_CACHE_MAX_TTL VAR_HARDEN_DNSSEC_STRIPPED VAR_ACCESS_CONTROL
%token VAR_LOCAL_ZONE VAR_LOCAL_DATA VAR_INTERFACE_AUTOMATIC
%token VAR_STATISTICS_INTERVAL VAR_DO_DAEMONIZE VAR_USE_CAPS_FOR_ID
%token VAR_STATISTICS_CUMULATIVE VAR_OUTGOING_PORT_PERMIT 
%token VAR_OUTGOING_PORT_AVOID VAR_DLV_ANCHOR_FILE VAR_DLV_ANCHOR
%token VAR_NEG_CACHE_SIZE VAR_HARDEN_REFERRAL_PATH VAR_PRIVATE_ADDRESS
%token VAR_PRIVATE_DOMAIN VAR_REMOTE_CONTROL VAR_CONTROL_ENABLE
%token VAR_CONTROL_INTERFACE VAR_CONTROL_PORT VAR_SERVER_KEY_FILE
%token VAR_SERVER_CERT_FILE VAR_CONTROL_KEY_FILE VAR_CONTROL_CERT_FILE
%token VAR_CONTROL_USE_CERT
%token VAR_EXTENDED_STATISTICS VAR_LOCAL_DATA_PTR VAR_JOSTLE_TIMEOUT
%token VAR_STUB_PRIME VAR_UNWANTED_REPLY_THRESHOLD VAR_LOG_TIME_ASCII
%token VAR_DOMAIN_INSECURE VAR_PYTHON VAR_PYTHON_SCRIPT VAR_VAL_SIG_SKEW_MIN
%token VAR_VAL_SIG_SKEW_MAX VAR_CACHE_MIN_TTL VAR_VAL_LOG_LEVEL
%token VAR_AUTO_TRUST_ANCHOR_FILE VAR_KEEP_MISSING VAR_ADD_HOLDDOWN 
%token VAR_DEL_HOLDDOWN VAR_SO_RCVBUF VAR_EDNS_BUFFER_SIZE VAR_PREFETCH
%token VAR_PREFETCH_KEY VAR_SO_SNDBUF VAR_SO_REUSEPORT VAR_HARDEN_BELOW_NXDOMAIN
%token VAR_IGNORE_CD_FLAG VAR_LOG_QUERIES VAR_LOG_REPLIES VAR_LOG_LOCAL_ACTIONS
%token VAR_TCP_UPSTREAM VAR_SSL_UPSTREAM
%token VAR_SSL_SERVICE_KEY VAR_SSL_SERVICE_PEM VAR_SSL_PORT VAR_FORWARD_FIRST
%token VAR_STUB_SSL_UPSTREAM VAR_FORWARD_SSL_UPSTREAM VAR_TLS_CERT_BUNDLE
%token VAR_STUB_FIRST VAR_MINIMAL_RESPONSES VAR_RRSET_ROUNDROBIN
%token VAR_MAX_UDP_SIZE VAR_DELAY_CLOSE
%token VAR_UNBLOCK_LAN_ZONES VAR_INSECURE_LAN_ZONES
%token VAR_INFRA_CACHE_MIN_RTT
%token VAR_DNS64_PREFIX VAR_DNS64_SYNTHALL VAR_DNS64_IGNORE_AAAA
%token VAR_DNSTAP VAR_DNSTAP_ENABLE VAR_DNSTAP_SOCKET_PATH
%token VAR_DNSTAP_SEND_IDENTITY VAR_DNSTAP_SEND_VERSION
%token VAR_DNSTAP_IDENTITY VAR_DNSTAP_VERSION
%token VAR_DNSTAP_LOG_RESOLVER_QUERY_MESSAGES
%token VAR_DNSTAP_LOG_RESOLVER_RESPONSE_MESSAGES
%token VAR_DNSTAP_LOG_CLIENT_QUERY_MESSAGES
%token VAR_DNSTAP_LOG_CLIENT_RESPONSE_MESSAGES
%token VAR_DNSTAP_LOG_FORWARDER_QUERY_MESSAGES
%token VAR_DNSTAP_LOG_FORWARDER_RESPONSE_MESSAGES
%token VAR_RESPONSE_IP_TAG VAR_RESPONSE_IP VAR_RESPONSE_IP_DATA
%token VAR_HARDEN_ALGO_DOWNGRADE VAR_IP_TRANSPARENT
%token VAR_DISABLE_DNSSEC_LAME_CHECK
%token VAR_IP_RATELIMIT VAR_IP_RATELIMIT_SLABS VAR_IP_RATELIMIT_SIZE
%token VAR_RATELIMIT VAR_RATELIMIT_SLABS VAR_RATELIMIT_SIZE
%token VAR_RATELIMIT_FOR_DOMAIN VAR_RATELIMIT_BELOW_DOMAIN
%token VAR_IP_RATELIMIT_FACTOR VAR_RATELIMIT_FACTOR
%token VAR_SEND_CLIENT_SUBNET VAR_CLIENT_SUBNET_ZONE
%token VAR_CLIENT_SUBNET_ALWAYS_FORWARD VAR_CLIENT_SUBNET_OPCODE
%token VAR_MAX_CLIENT_SUBNET_IPV4 VAR_MAX_CLIENT_SUBNET_IPV6
%token VAR_CAPS_WHITELIST VAR_CACHE_MAX_NEGATIVE_TTL VAR_PERMIT_SMALL_HOLDDOWN
%token VAR_QNAME_MINIMISATION VAR_QNAME_MINIMISATION_STRICT VAR_IP_FREEBIND
%token VAR_DEFINE_TAG VAR_LOCAL_ZONE_TAG VAR_ACCESS_CONTROL_TAG
%token VAR_LOCAL_ZONE_OVERRIDE VAR_ACCESS_CONTROL_TAG_ACTION
%token VAR_ACCESS_CONTROL_TAG_DATA VAR_VIEW VAR_ACCESS_CONTROL_VIEW
%token VAR_VIEW_FIRST VAR_SERVE_EXPIRED VAR_SERVE_EXPIRED_TTL
%token VAR_SERVE_EXPIRED_TTL_RESET VAR_FAKE_DSA VAR_FAKE_SHA1
%token VAR_LOG_IDENTITY VAR_HIDE_TRUSTANCHOR VAR_TRUST_ANCHOR_SIGNALING
%token VAR_AGGRESSIVE_NSEC VAR_USE_SYSTEMD VAR_SHM_ENABLE VAR_SHM_KEY
%token VAR_ROOT_KEY_SENTINEL
%token VAR_DNSCRYPT VAR_DNSCRYPT_ENABLE VAR_DNSCRYPT_PORT VAR_DNSCRYPT_PROVIDER
%token VAR_DNSCRYPT_SECRET_KEY VAR_DNSCRYPT_PROVIDER_CERT
%token VAR_DNSCRYPT_PROVIDER_CERT_ROTATED
%token VAR_DNSCRYPT_SHARED_SECRET_CACHE_SIZE
%token VAR_DNSCRYPT_SHARED_SECRET_CACHE_SLABS
%token VAR_DNSCRYPT_NONCE_CACHE_SIZE
%token VAR_DNSCRYPT_NONCE_CACHE_SLABS
%token VAR_IPSECMOD_ENABLED VAR_IPSECMOD_HOOK VAR_IPSECMOD_IGNORE_BOGUS
%token VAR_IPSECMOD_MAX_TTL VAR_IPSECMOD_WHITELIST VAR_IPSECMOD_STRICT
%token VAR_CACHEDB VAR_CACHEDB_BACKEND VAR_CACHEDB_SECRETSEED
%token VAR_CACHEDB_REDISHOST VAR_CACHEDB_REDISPORT VAR_CACHEDB_REDISTIMEOUT
%token VAR_UDP_UPSTREAM_WITHOUT_DOWNSTREAM VAR_FOR_UPSTREAM
%token VAR_AUTH_ZONE VAR_ZONEFILE VAR_MASTER VAR_URL VAR_FOR_DOWNSTREAM
%token VAR_FALLBACK_ENABLED VAR_TLS_ADDITIONAL_PORT VAR_LOW_RTT VAR_LOW_RTT_PERMIL
%token VAR_ALLOW_NOTIFY VAR_TLS_WIN_CERT VAR_TCP_CONNECTION_LIMIT
%token VAR_FORWARD_NO_CACHE VAR_STUB_NO_CACHE VAR_LOG_SERVFAIL

%%
toplevelvars: /* empty */ | toplevelvars toplevelvar ;
toplevelvar: serverstart contents_server | stubstart contents_stub |
	forwardstart contents_forward | pythonstart contents_py | 
	rcstart contents_rc | dtstart contents_dt | viewstart contents_view |
	dnscstart contents_dnsc | cachedbstart contents_cachedb |
	authstart contents_auth
	;

/* server: declaration */
serverstart: VAR_SERVER
	{ 
		OUTYY(("\nP(server:)\n")); 
	}
	;
contents_server: contents_server content_server 
	| ;
content_server: server_num_threads | server_verbosity | server_port |
	server_outgoing_range | server_do_ip4 |
	server_do_ip6 | server_prefer_ip6 |
	server_do_udp | server_do_tcp |
	server_tcp_mss | server_outgoing_tcp_mss | server_tcp_idle_timeout |
	server_tcp_keepalive | server_tcp_keepalive_timeout |
	server_interface | server_chroot | server_username | 
	server_directory | server_logfile | server_pidfile |
	server_msg_cache_size | server_msg_cache_slabs |
	server_num_queries_per_thread | server_rrset_cache_size | 
	server_rrset_cache_slabs | server_outgoing_num_tcp | 
	server_infra_host_ttl | server_infra_lame_ttl | 
	server_infra_cache_slabs | server_infra_cache_numhosts |
	server_infra_cache_lame_size | server_target_fetch_policy | 
	server_harden_short_bufsize | server_harden_large_queries |
	server_do_not_query_address | server_hide_identity |
	server_hide_version | server_identity | server_version |
	server_harden_glue | server_module_conf | server_trust_anchor_file |
	server_trust_anchor | server_val_override_date | server_bogus_ttl |
	server_val_clean_additional | server_val_permissive_mode |
	server_incoming_num_tcp | server_msg_buffer_size | 
	server_key_cache_size | server_key_cache_slabs | 
	server_trusted_keys_file | server_val_nsec3_keysize_iterations |
	server_use_syslog | server_outgoing_interface | server_root_hints |
	server_do_not_query_localhost | server_cache_max_ttl |
	server_harden_dnssec_stripped | server_access_control |
	server_local_zone | server_local_data | server_interface_automatic |
	server_statistics_interval | server_do_daemonize | 
	server_use_caps_for_id | server_statistics_cumulative |
	server_outgoing_port_permit | server_outgoing_port_avoid |
	server_dlv_anchor_file | server_dlv_anchor | server_neg_cache_size |
	server_harden_referral_path | server_private_address |
	server_private_domain | server_extended_statistics | 
	server_local_data_ptr | server_jostle_timeout | 
	server_unwanted_reply_threshold | server_log_time_ascii | 
	server_domain_insecure | server_val_sig_skew_min | 
	server_val_sig_skew_max | server_cache_min_ttl | server_val_log_level |
	server_auto_trust_anchor_file | server_add_holddown | 
	server_del_holddown | server_keep_missing | server_so_rcvbuf |
	server_edns_buffer_size | server_prefetch | server_prefetch_key |
	server_so_sndbuf | server_harden_below_nxdomain | server_ignore_cd_flag |
	server_log_queries | server_log_replies | server_tcp_upstream | server_ssl_upstream |
	server_log_local_actions |
	server_ssl_service_key | server_ssl_service_pem | server_ssl_port |
	server_minimal_responses | server_rrset_roundrobin | server_max_udp_size |
	server_so_reuseport | server_delay_close |
	server_unblock_lan_zones | server_insecure_lan_zones |
	server_dns64_prefix | server_dns64_synthall | server_dns64_ignore_aaaa |
	server_infra_cache_min_rtt | server_harden_algo_downgrade |
	server_ip_transparent | server_ip_ratelimit | server_ratelimit |
	server_ip_ratelimit_slabs | server_ratelimit_slabs |
	server_ip_ratelimit_size | server_ratelimit_size |
	server_ratelimit_for_domain |
	server_ratelimit_below_domain | server_ratelimit_factor |
	server_ip_ratelimit_factor | server_send_client_subnet |
	server_client_subnet_zone | server_client_subnet_always_forward |
	server_client_subnet_opcode |
	server_max_client_subnet_ipv4 | server_max_client_subnet_ipv6 |
	server_caps_whitelist | server_cache_max_negative_ttl |
	server_permit_small_holddown | server_qname_minimisation |
	server_ip_freebind | server_define_tag | server_local_zone_tag |
	server_disable_dnssec_lame_check | server_access_control_tag |
	server_local_zone_override | server_access_control_tag_action |
	server_access_control_tag_data | server_access_control_view |
	server_qname_minimisation_strict | server_serve_expired |
	server_serve_expired_ttl | server_serve_expired_ttl_reset |
	server_fake_dsa | server_log_identity | server_use_systemd |
	server_response_ip_tag | server_response_ip | server_response_ip_data |
	server_shm_enable | server_shm_key | server_fake_sha1 |
	server_hide_trustanchor | server_trust_anchor_signaling |
	server_root_key_sentinel |
	server_ipsecmod_enabled | server_ipsecmod_hook |
	server_ipsecmod_ignore_bogus | server_ipsecmod_max_ttl |
	server_ipsecmod_whitelist | server_ipsecmod_strict |
	server_udp_upstream_without_downstream | server_aggressive_nsec |
	server_tls_cert_bundle | server_tls_additional_port | server_low_rtt |
	server_low_rtt_permil | server_tls_win_cert |
	server_tcp_connection_limit | server_log_servfail
	;
stubstart: VAR_STUB_ZONE
	{
		struct config_stub* s;
		OUTYY(("\nP(stub_zone:)\n")); 
		s = (struct config_stub*)calloc(1, sizeof(struct config_stub));
		if(s) {
			s->next = cfg_parser->cfg->stubs;
			cfg_parser->cfg->stubs = s;
		} else 
			yyerror("out of memory");
	}
	;
contents_stub: contents_stub content_stub 
	| ;
content_stub: stub_name | stub_host | stub_addr | stub_prime | stub_first |
	stub_no_cache | stub_ssl_upstream
	;
forwardstart: VAR_FORWARD_ZONE
	{
		struct config_stub* s;
		OUTYY(("\nP(forward_zone:)\n")); 
		s = (struct config_stub*)calloc(1, sizeof(struct config_stub));
		if(s) {
			s->next = cfg_parser->cfg->forwards;
			cfg_parser->cfg->forwards = s;
		} else 
			yyerror("out of memory");
	}
	;
contents_forward: contents_forward content_forward 
	| ;
content_forward: forward_name | forward_host | forward_addr | forward_first |
	forward_no_cache | forward_ssl_upstream
	;
viewstart: VAR_VIEW
	{
		struct config_view* s;
		OUTYY(("\nP(view:)\n")); 
		s = (struct config_view*)calloc(1, sizeof(struct config_view));
		if(s) {
			s->next = cfg_parser->cfg->views;
			if(s->next && !s->next->name)
				yyerror("view without name");
			cfg_parser->cfg->views = s;
		} else 
			yyerror("out of memory");
	}
	;
contents_view: contents_view content_view 
	| ;
content_view: view_name | view_local_zone | view_local_data | view_first |
		view_response_ip | view_response_ip_data | view_local_data_ptr
	;
authstart: VAR_AUTH_ZONE
	{
		struct config_auth* s;
		OUTYY(("\nP(auth_zone:)\n")); 
		s = (struct config_auth*)calloc(1, sizeof(struct config_auth));
		if(s) {
			s->next = cfg_parser->cfg->auths;
			cfg_parser->cfg->auths = s;
			/* defaults for auth zone */
			s->for_downstream = 1;
			s->for_upstream = 1;
			s->fallback_enabled = 0;
		} else 
			yyerror("out of memory");
	}
	;
contents_auth: contents_auth content_auth 
	| ;
content_auth: auth_name | auth_zonefile | auth_master | auth_url |
	auth_for_downstream | auth_for_upstream | auth_fallback_enabled |
	auth_allow_notify
	;
server_num_threads: VAR_NUM_THREADS STRING_ARG 
	{ 
		OUTYY(("P(server_num_threads:%s)\n", $2)); 
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->num_threads = atoi($2);
		free($2);
	}
	;
server_verbosity: VAR_VERBOSITY STRING_ARG 
	{ 
		OUTYY(("P(server_verbosity:%s)\n", $2)); 
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->verbosity = atoi($2);
		free($2);
	}
	;
server_statistics_interval: VAR_STATISTICS_INTERVAL STRING_ARG 
	{ 
		OUTYY(("P(server_statistics_interval:%s)\n", $2)); 
		if(strcmp($2, "") == 0 || strcmp($2, "0") == 0)
			cfg_parser->cfg->stat_interval = 0;
		else if(atoi($2) == 0)
			yyerror("number expected");
		else cfg_parser->cfg->stat_interval = atoi($2);
		free($2);
	}
	;
server_statistics_cumulative: VAR_STATISTICS_CUMULATIVE STRING_ARG
	{
		OUTYY(("P(server_statistics_cumulative:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->stat_cumulative = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_extended_statistics: VAR_EXTENDED_STATISTICS STRING_ARG
	{
		OUTYY(("P(server_extended_statistics:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->stat_extended = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_shm_enable: VAR_SHM_ENABLE STRING_ARG
	{
		OUTYY(("P(server_shm_enable:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->shm_enable = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_shm_key: VAR_SHM_KEY STRING_ARG 
	{ 
		OUTYY(("P(server_shm_key:%s)\n", $2)); 
		if(strcmp($2, "") == 0 || strcmp($2, "0") == 0)
			cfg_parser->cfg->shm_key = 0;
		else if(atoi($2) == 0)
			yyerror("number expected");
		else cfg_parser->cfg->shm_key = atoi($2);
		free($2);
	}
	;
server_port: VAR_PORT STRING_ARG
	{
		OUTYY(("P(server_port:%s)\n", $2));
		if(atoi($2) == 0)
			yyerror("port number expected");
		else cfg_parser->cfg->port = atoi($2);
		free($2);
	}
	;
server_send_client_subnet: VAR_SEND_CLIENT_SUBNET STRING_ARG
	{
	#ifdef CLIENT_SUBNET
		OUTYY(("P(server_send_client_subnet:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->client_subnet, $2))
			fatal_exit("out of memory adding client-subnet");
	#else
		OUTYY(("P(Compiled without edns subnet option, ignoring)\n"));
	#endif
	}
	;
server_client_subnet_zone: VAR_CLIENT_SUBNET_ZONE STRING_ARG
	{
	#ifdef CLIENT_SUBNET
		OUTYY(("P(server_client_subnet_zone:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->client_subnet_zone,
			$2))
			fatal_exit("out of memory adding client-subnet-zone");
	#else
		OUTYY(("P(Compiled without edns subnet option, ignoring)\n"));
	#endif
	}
	;
server_client_subnet_always_forward:
	VAR_CLIENT_SUBNET_ALWAYS_FORWARD STRING_ARG
	{
	#ifdef CLIENT_SUBNET
		OUTYY(("P(server_client_subnet_always_forward:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else
			cfg_parser->cfg->client_subnet_always_forward =
				(strcmp($2, "yes")==0);
	#else
		OUTYY(("P(Compiled without edns subnet option, ignoring)\n"));
	#endif
		free($2);
	}
	;
server_client_subnet_opcode: VAR_CLIENT_SUBNET_OPCODE STRING_ARG
	{
	#ifdef CLIENT_SUBNET
		OUTYY(("P(client_subnet_opcode:%s)\n", $2));
		OUTYY(("P(Deprecated option, ignoring)\n"));
	#else
		OUTYY(("P(Compiled without edns subnet option, ignoring)\n"));
	#endif
		free($2);
	}
	;
server_max_client_subnet_ipv4: VAR_MAX_CLIENT_SUBNET_IPV4 STRING_ARG
	{
	#ifdef CLIENT_SUBNET
		OUTYY(("P(max_client_subnet_ipv4:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("IPv4 subnet length expected");
		else if (atoi($2) > 32)
			cfg_parser->cfg->max_client_subnet_ipv4 = 32;
		else if (atoi($2) < 0)
			cfg_parser->cfg->max_client_subnet_ipv4 = 0;
		else cfg_parser->cfg->max_client_subnet_ipv4 = (uint8_t)atoi($2);
	#else
		OUTYY(("P(Compiled without edns subnet option, ignoring)\n"));
	#endif
		free($2);
	}
	;
server_max_client_subnet_ipv6: VAR_MAX_CLIENT_SUBNET_IPV6 STRING_ARG
	{
	#ifdef CLIENT_SUBNET
		OUTYY(("P(max_client_subnet_ipv6:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("Ipv6 subnet length expected");
		else if (atoi($2) > 128)
			cfg_parser->cfg->max_client_subnet_ipv6 = 128;
		else if (atoi($2) < 0)
			cfg_parser->cfg->max_client_subnet_ipv6 = 0;
		else cfg_parser->cfg->max_client_subnet_ipv6 = (uint8_t)atoi($2);
	#else
		OUTYY(("P(Compiled without edns subnet option, ignoring)\n"));
	#endif
		free($2);
	}
	;
server_interface: VAR_INTERFACE STRING_ARG
	{
		OUTYY(("P(server_interface:%s)\n", $2));
		if(cfg_parser->cfg->num_ifs == 0)
			cfg_parser->cfg->ifs = calloc(1, sizeof(char*));
		else 	cfg_parser->cfg->ifs = realloc(cfg_parser->cfg->ifs,
				(cfg_parser->cfg->num_ifs+1)*sizeof(char*));
		if(!cfg_parser->cfg->ifs)
			yyerror("out of memory");
		else
			cfg_parser->cfg->ifs[cfg_parser->cfg->num_ifs++] = $2;
	}
	;
server_outgoing_interface: VAR_OUTGOING_INTERFACE STRING_ARG
	{
		OUTYY(("P(server_outgoing_interface:%s)\n", $2));
		if(cfg_parser->cfg->num_out_ifs == 0)
			cfg_parser->cfg->out_ifs = calloc(1, sizeof(char*));
		else 	cfg_parser->cfg->out_ifs = realloc(
			cfg_parser->cfg->out_ifs, 
			(cfg_parser->cfg->num_out_ifs+1)*sizeof(char*));
		if(!cfg_parser->cfg->out_ifs)
			yyerror("out of memory");
		else
			cfg_parser->cfg->out_ifs[
				cfg_parser->cfg->num_out_ifs++] = $2;
	}
	;
server_outgoing_range: VAR_OUTGOING_RANGE STRING_ARG
	{
		OUTYY(("P(server_outgoing_range:%s)\n", $2));
		if(atoi($2) == 0)
			yyerror("number expected");
		else cfg_parser->cfg->outgoing_num_ports = atoi($2);
		free($2);
	}
	;
server_outgoing_port_permit: VAR_OUTGOING_PORT_PERMIT STRING_ARG
	{
		OUTYY(("P(server_outgoing_port_permit:%s)\n", $2));
		if(!cfg_mark_ports($2, 1, 
			cfg_parser->cfg->outgoing_avail_ports, 65536))
			yyerror("port number or range (\"low-high\") expected");
		free($2);
	}
	;
server_outgoing_port_avoid: VAR_OUTGOING_PORT_AVOID STRING_ARG
	{
		OUTYY(("P(server_outgoing_port_avoid:%s)\n", $2));
		if(!cfg_mark_ports($2, 0, 
			cfg_parser->cfg->outgoing_avail_ports, 65536))
			yyerror("port number or range (\"low-high\") expected");
		free($2);
	}
	;
server_outgoing_num_tcp: VAR_OUTGOING_NUM_TCP STRING_ARG
	{
		OUTYY(("P(server_outgoing_num_tcp:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->outgoing_num_tcp = atoi($2);
		free($2);
	}
	;
server_incoming_num_tcp: VAR_INCOMING_NUM_TCP STRING_ARG
	{
		OUTYY(("P(server_incoming_num_tcp:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->incoming_num_tcp = atoi($2);
		free($2);
	}
	;
server_interface_automatic: VAR_INTERFACE_AUTOMATIC STRING_ARG
	{
		OUTYY(("P(server_interface_automatic:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->if_automatic = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_do_ip4: VAR_DO_IP4 STRING_ARG
	{
		OUTYY(("P(server_do_ip4:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->do_ip4 = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_do_ip6: VAR_DO_IP6 STRING_ARG
	{
		OUTYY(("P(server_do_ip6:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->do_ip6 = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_do_udp: VAR_DO_UDP STRING_ARG
	{
		OUTYY(("P(server_do_udp:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->do_udp = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_do_tcp: VAR_DO_TCP STRING_ARG
	{
		OUTYY(("P(server_do_tcp:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->do_tcp = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_prefer_ip6: VAR_PREFER_IP6 STRING_ARG
	{
		OUTYY(("P(server_prefer_ip6:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->prefer_ip6 = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_tcp_mss: VAR_TCP_MSS STRING_ARG
	{
		OUTYY(("P(server_tcp_mss:%s)\n", $2));
                if(atoi($2) == 0 && strcmp($2, "0") != 0)
                        yyerror("number expected");
                else cfg_parser->cfg->tcp_mss = atoi($2);
                free($2);
	}
	;
server_outgoing_tcp_mss: VAR_OUTGOING_TCP_MSS STRING_ARG
	{
		OUTYY(("P(server_outgoing_tcp_mss:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->outgoing_tcp_mss = atoi($2);
		free($2);
	}
	;
server_tcp_idle_timeout: VAR_TCP_IDLE_TIMEOUT STRING_ARG
	{
		OUTYY(("P(server_tcp_idle_timeout:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else if (atoi($2) > 120000)
			cfg_parser->cfg->tcp_idle_timeout = 120000;
		else if (atoi($2) < 1)
			cfg_parser->cfg->tcp_idle_timeout = 1;
		else cfg_parser->cfg->tcp_idle_timeout = atoi($2);
		free($2);
	}
	;
server_tcp_keepalive: VAR_EDNS_TCP_KEEPALIVE STRING_ARG
	{
		OUTYY(("P(server_tcp_keepalive:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->do_tcp_keepalive = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_tcp_keepalive_timeout: VAR_EDNS_TCP_KEEPALIVE_TIMEOUT STRING_ARG
	{
		OUTYY(("P(server_tcp_keepalive_timeout:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else if (atoi($2) > 6553500)
			cfg_parser->cfg->tcp_keepalive_timeout = 6553500;
		else if (atoi($2) < 1)
			cfg_parser->cfg->tcp_keepalive_timeout = 0;
		else cfg_parser->cfg->tcp_keepalive_timeout = atoi($2);
		free($2);
	}
	;
server_tcp_upstream: VAR_TCP_UPSTREAM STRING_ARG
	{
		OUTYY(("P(server_tcp_upstream:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->tcp_upstream = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_udp_upstream_without_downstream: VAR_UDP_UPSTREAM_WITHOUT_DOWNSTREAM STRING_ARG
	{
		OUTYY(("P(server_udp_upstream_without_downstream:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->udp_upstream_without_downstream = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_ssl_upstream: VAR_SSL_UPSTREAM STRING_ARG
	{
		OUTYY(("P(server_ssl_upstream:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->ssl_upstream = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_ssl_service_key: VAR_SSL_SERVICE_KEY STRING_ARG
	{
		OUTYY(("P(server_ssl_service_key:%s)\n", $2));
		free(cfg_parser->cfg->ssl_service_key);
		cfg_parser->cfg->ssl_service_key = $2;
	}
	;
server_ssl_service_pem: VAR_SSL_SERVICE_PEM STRING_ARG
	{
		OUTYY(("P(server_ssl_service_pem:%s)\n", $2));
		free(cfg_parser->cfg->ssl_service_pem);
		cfg_parser->cfg->ssl_service_pem = $2;
	}
	;
server_ssl_port: VAR_SSL_PORT STRING_ARG
	{
		OUTYY(("P(server_ssl_port:%s)\n", $2));
		if(atoi($2) == 0)
			yyerror("port number expected");
		else cfg_parser->cfg->ssl_port = atoi($2);
		free($2);
	}
	;
server_tls_cert_bundle: VAR_TLS_CERT_BUNDLE STRING_ARG
	{
		OUTYY(("P(server_tls_cert_bundle:%s)\n", $2));
		free(cfg_parser->cfg->tls_cert_bundle);
		cfg_parser->cfg->tls_cert_bundle = $2;
	}
	;
server_tls_win_cert: VAR_TLS_WIN_CERT STRING_ARG
	{
		OUTYY(("P(server_tls_win_cert:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->tls_win_cert = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_tls_additional_port: VAR_TLS_ADDITIONAL_PORT STRING_ARG
	{
		OUTYY(("P(server_tls_additional_port:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->tls_additional_port,
			$2))
			yyerror("out of memory");
	}
	;
server_use_systemd: VAR_USE_SYSTEMD STRING_ARG
	{
		OUTYY(("P(server_use_systemd:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->use_systemd = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_do_daemonize: VAR_DO_DAEMONIZE STRING_ARG
	{
		OUTYY(("P(server_do_daemonize:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->do_daemonize = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_use_syslog: VAR_USE_SYSLOG STRING_ARG
	{
		OUTYY(("P(server_use_syslog:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->use_syslog = (strcmp($2, "yes")==0);
#if !defined(HAVE_SYSLOG_H) && !defined(UB_ON_WINDOWS)
		if(strcmp($2, "yes") == 0)
			yyerror("no syslog services are available. "
				"(reconfigure and compile to add)");
#endif
		free($2);
	}
	;
server_log_time_ascii: VAR_LOG_TIME_ASCII STRING_ARG
	{
		OUTYY(("P(server_log_time_ascii:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->log_time_ascii = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_log_queries: VAR_LOG_QUERIES STRING_ARG
	{
		OUTYY(("P(server_log_queries:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->log_queries = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_log_replies: VAR_LOG_REPLIES STRING_ARG
  {
  	OUTYY(("P(server_log_replies:%s)\n", $2));
  	if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
  		yyerror("expected yes or no.");
  	else cfg_parser->cfg->log_replies = (strcmp($2, "yes")==0);
  	free($2);
  }
  ;
server_log_servfail: VAR_LOG_SERVFAIL STRING_ARG
	{
		OUTYY(("P(server_log_servfail:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->log_servfail = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_log_local_actions: VAR_LOG_LOCAL_ACTIONS STRING_ARG
  {
  	OUTYY(("P(server_log_local_actions:%s)\n", $2));
  	if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
  		yyerror("expected yes or no.");
  	else cfg_parser->cfg->log_local_actions = (strcmp($2, "yes")==0);
  	free($2);
  }
  ;
server_chroot: VAR_CHROOT STRING_ARG
	{
		OUTYY(("P(server_chroot:%s)\n", $2));
		free(cfg_parser->cfg->chrootdir);
		cfg_parser->cfg->chrootdir = $2;
	}
	;
server_username: VAR_USERNAME STRING_ARG
	{
		OUTYY(("P(server_username:%s)\n", $2));
		free(cfg_parser->cfg->username);
		cfg_parser->cfg->username = $2;
	}
	;
server_directory: VAR_DIRECTORY STRING_ARG
	{
		OUTYY(("P(server_directory:%s)\n", $2));
		free(cfg_parser->cfg->directory);
		cfg_parser->cfg->directory = $2;
		/* change there right away for includes relative to this */
		if($2[0]) {
			char* d;
#ifdef UB_ON_WINDOWS
			w_config_adjust_directory(cfg_parser->cfg);
#endif
			d = cfg_parser->cfg->directory;
			/* adjust directory if we have already chroot,
			 * like, we reread after sighup */
			if(cfg_parser->chroot && cfg_parser->chroot[0] &&
				strncmp(d, cfg_parser->chroot, strlen(
				cfg_parser->chroot)) == 0)
				d += strlen(cfg_parser->chroot);
			if(d[0]) {
			    if(chdir(d))
				log_err("cannot chdir to directory: %s (%s)",
					d, strerror(errno));
			}
		}
	}
	;
server_logfile: VAR_LOGFILE STRING_ARG
	{
		OUTYY(("P(server_logfile:%s)\n", $2));
		free(cfg_parser->cfg->logfile);
		cfg_parser->cfg->logfile = $2;
		cfg_parser->cfg->use_syslog = 0;
	}
	;
server_pidfile: VAR_PIDFILE STRING_ARG
	{
		OUTYY(("P(server_pidfile:%s)\n", $2));
		free(cfg_parser->cfg->pidfile);
		cfg_parser->cfg->pidfile = $2;
	}
	;
server_root_hints: VAR_ROOT_HINTS STRING_ARG
	{
		OUTYY(("P(server_root_hints:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->root_hints, $2))
			yyerror("out of memory");
	}
	;
server_dlv_anchor_file: VAR_DLV_ANCHOR_FILE STRING_ARG
	{
		OUTYY(("P(server_dlv_anchor_file:%s)\n", $2));
		free(cfg_parser->cfg->dlv_anchor_file);
		cfg_parser->cfg->dlv_anchor_file = $2;
	}
	;
server_dlv_anchor: VAR_DLV_ANCHOR STRING_ARG
	{
		OUTYY(("P(server_dlv_anchor:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->dlv_anchor_list, $2))
			yyerror("out of memory");
	}
	;
server_auto_trust_anchor_file: VAR_AUTO_TRUST_ANCHOR_FILE STRING_ARG
	{
		OUTYY(("P(server_auto_trust_anchor_file:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->
			auto_trust_anchor_file_list, $2))
			yyerror("out of memory");
	}
	;
server_trust_anchor_file: VAR_TRUST_ANCHOR_FILE STRING_ARG
	{
		OUTYY(("P(server_trust_anchor_file:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->
			trust_anchor_file_list, $2))
			yyerror("out of memory");
	}
	;
server_trusted_keys_file: VAR_TRUSTED_KEYS_FILE STRING_ARG
	{
		OUTYY(("P(server_trusted_keys_file:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->
			trusted_keys_file_list, $2))
			yyerror("out of memory");
	}
	;
server_trust_anchor: VAR_TRUST_ANCHOR STRING_ARG
	{
		OUTYY(("P(server_trust_anchor:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->trust_anchor_list, $2))
			yyerror("out of memory");
	}
	;
server_trust_anchor_signaling: VAR_TRUST_ANCHOR_SIGNALING STRING_ARG
	{
		OUTYY(("P(server_trust_anchor_signaling:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else
			cfg_parser->cfg->trust_anchor_signaling =
				(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_root_key_sentinel: VAR_ROOT_KEY_SENTINEL STRING_ARG
	{
		OUTYY(("P(server_root_key_sentinel:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else
			cfg_parser->cfg->root_key_sentinel =
				(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_domain_insecure: VAR_DOMAIN_INSECURE STRING_ARG
	{
		OUTYY(("P(server_domain_insecure:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->domain_insecure, $2))
			yyerror("out of memory");
	}
	;
server_hide_identity: VAR_HIDE_IDENTITY STRING_ARG
	{
		OUTYY(("P(server_hide_identity:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->hide_identity = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_hide_version: VAR_HIDE_VERSION STRING_ARG
	{
		OUTYY(("P(server_hide_version:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->hide_version = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_hide_trustanchor: VAR_HIDE_TRUSTANCHOR STRING_ARG
	{
		OUTYY(("P(server_hide_trustanchor:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->hide_trustanchor = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_identity: VAR_IDENTITY STRING_ARG
	{
		OUTYY(("P(server_identity:%s)\n", $2));
		free(cfg_parser->cfg->identity);
		cfg_parser->cfg->identity = $2;
	}
	;
server_version: VAR_VERSION STRING_ARG
	{
		OUTYY(("P(server_version:%s)\n", $2));
		free(cfg_parser->cfg->version);
		cfg_parser->cfg->version = $2;
	}
	;
server_so_rcvbuf: VAR_SO_RCVBUF STRING_ARG
	{
		OUTYY(("P(server_so_rcvbuf:%s)\n", $2));
		if(!cfg_parse_memsize($2, &cfg_parser->cfg->so_rcvbuf))
			yyerror("buffer size expected");
		free($2);
	}
	;
server_so_sndbuf: VAR_SO_SNDBUF STRING_ARG
	{
		OUTYY(("P(server_so_sndbuf:%s)\n", $2));
		if(!cfg_parse_memsize($2, &cfg_parser->cfg->so_sndbuf))
			yyerror("buffer size expected");
		free($2);
	}
	;
server_so_reuseport: VAR_SO_REUSEPORT STRING_ARG
    {
        OUTYY(("P(server_so_reuseport:%s)\n", $2));
        if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
            yyerror("expected yes or no.");
        else cfg_parser->cfg->so_reuseport =
            (strcmp($2, "yes")==0);
        free($2);
    }
    ;
server_ip_transparent: VAR_IP_TRANSPARENT STRING_ARG
    {
        OUTYY(("P(server_ip_transparent:%s)\n", $2));
        if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
            yyerror("expected yes or no.");
        else cfg_parser->cfg->ip_transparent =
            (strcmp($2, "yes")==0);
        free($2);
    }
    ;
server_ip_freebind: VAR_IP_FREEBIND STRING_ARG
    {
        OUTYY(("P(server_ip_freebind:%s)\n", $2));
        if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
            yyerror("expected yes or no.");
        else cfg_parser->cfg->ip_freebind =
            (strcmp($2, "yes")==0);
        free($2);
    }
    ;
server_edns_buffer_size: VAR_EDNS_BUFFER_SIZE STRING_ARG
	{
		OUTYY(("P(server_edns_buffer_size:%s)\n", $2));
		if(atoi($2) == 0)
			yyerror("number expected");
		else if (atoi($2) < 12)
			yyerror("edns buffer size too small");
		else if (atoi($2) > 65535)
			cfg_parser->cfg->edns_buffer_size = 65535;
		else cfg_parser->cfg->edns_buffer_size = atoi($2);
		free($2);
	}
	;
server_msg_buffer_size: VAR_MSG_BUFFER_SIZE STRING_ARG
	{
		OUTYY(("P(server_msg_buffer_size:%s)\n", $2));
		if(atoi($2) == 0)
			yyerror("number expected");
		else if (atoi($2) < 4096)
			yyerror("message buffer size too small (use 4096)");
		else cfg_parser->cfg->msg_buffer_size = atoi($2);
		free($2);
	}
	;
server_msg_cache_size: VAR_MSG_CACHE_SIZE STRING_ARG
	{
		OUTYY(("P(server_msg_cache_size:%s)\n", $2));
		if(!cfg_parse_memsize($2, &cfg_parser->cfg->msg_cache_size))
			yyerror("memory size expected");
		free($2);
	}
	;
server_msg_cache_slabs: VAR_MSG_CACHE_SLABS STRING_ARG
	{
		OUTYY(("P(server_msg_cache_slabs:%s)\n", $2));
		if(atoi($2) == 0)
			yyerror("number expected");
		else {
			cfg_parser->cfg->msg_cache_slabs = atoi($2);
			if(!is_pow2(cfg_parser->cfg->msg_cache_slabs))
				yyerror("must be a power of 2");
		}
		free($2);
	}
	;
server_num_queries_per_thread: VAR_NUM_QUERIES_PER_THREAD STRING_ARG
	{
		OUTYY(("P(server_num_queries_per_thread:%s)\n", $2));
		if(atoi($2) == 0)
			yyerror("number expected");
		else cfg_parser->cfg->num_queries_per_thread = atoi($2);
		free($2);
	}
	;
server_jostle_timeout: VAR_JOSTLE_TIMEOUT STRING_ARG
	{
		OUTYY(("P(server_jostle_timeout:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->jostle_time = atoi($2);
		free($2);
	}
	;
server_delay_close: VAR_DELAY_CLOSE STRING_ARG
	{
		OUTYY(("P(server_delay_close:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->delay_close = atoi($2);
		free($2);
	}
	;
server_unblock_lan_zones: VAR_UNBLOCK_LAN_ZONES STRING_ARG
	{
		OUTYY(("P(server_unblock_lan_zones:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->unblock_lan_zones = 
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_insecure_lan_zones: VAR_INSECURE_LAN_ZONES STRING_ARG
	{
		OUTYY(("P(server_insecure_lan_zones:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->insecure_lan_zones = 
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_rrset_cache_size: VAR_RRSET_CACHE_SIZE STRING_ARG
	{
		OUTYY(("P(server_rrset_cache_size:%s)\n", $2));
		if(!cfg_parse_memsize($2, &cfg_parser->cfg->rrset_cache_size))
			yyerror("memory size expected");
		free($2);
	}
	;
server_rrset_cache_slabs: VAR_RRSET_CACHE_SLABS STRING_ARG
	{
		OUTYY(("P(server_rrset_cache_slabs:%s)\n", $2));
		if(atoi($2) == 0)
			yyerror("number expected");
		else {
			cfg_parser->cfg->rrset_cache_slabs = atoi($2);
			if(!is_pow2(cfg_parser->cfg->rrset_cache_slabs))
				yyerror("must be a power of 2");
		}
		free($2);
	}
	;
server_infra_host_ttl: VAR_INFRA_HOST_TTL STRING_ARG
	{
		OUTYY(("P(server_infra_host_ttl:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->host_ttl = atoi($2);
		free($2);
	}
	;
server_infra_lame_ttl: VAR_INFRA_LAME_TTL STRING_ARG
	{
		OUTYY(("P(server_infra_lame_ttl:%s)\n", $2));
		verbose(VERB_DETAIL, "ignored infra-lame-ttl: %s (option "
			"removed, use infra-host-ttl)", $2);
		free($2);
	}
	;
server_infra_cache_numhosts: VAR_INFRA_CACHE_NUMHOSTS STRING_ARG
	{
		OUTYY(("P(server_infra_cache_numhosts:%s)\n", $2));
		if(atoi($2) == 0)
			yyerror("number expected");
		else cfg_parser->cfg->infra_cache_numhosts = atoi($2);
		free($2);
	}
	;
server_infra_cache_lame_size: VAR_INFRA_CACHE_LAME_SIZE STRING_ARG
	{
		OUTYY(("P(server_infra_cache_lame_size:%s)\n", $2));
		verbose(VERB_DETAIL, "ignored infra-cache-lame-size: %s "
			"(option removed, use infra-cache-numhosts)", $2);
		free($2);
	}
	;
server_infra_cache_slabs: VAR_INFRA_CACHE_SLABS STRING_ARG
	{
		OUTYY(("P(server_infra_cache_slabs:%s)\n", $2));
		if(atoi($2) == 0)
			yyerror("number expected");
		else {
			cfg_parser->cfg->infra_cache_slabs = atoi($2);
			if(!is_pow2(cfg_parser->cfg->infra_cache_slabs))
				yyerror("must be a power of 2");
		}
		free($2);
	}
	;
server_infra_cache_min_rtt: VAR_INFRA_CACHE_MIN_RTT STRING_ARG
	{
		OUTYY(("P(server_infra_cache_min_rtt:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->infra_cache_min_rtt = atoi($2);
		free($2);
	}
	;
server_target_fetch_policy: VAR_TARGET_FETCH_POLICY STRING_ARG
	{
		OUTYY(("P(server_target_fetch_policy:%s)\n", $2));
		free(cfg_parser->cfg->target_fetch_policy);
		cfg_parser->cfg->target_fetch_policy = $2;
	}
	;
server_harden_short_bufsize: VAR_HARDEN_SHORT_BUFSIZE STRING_ARG
	{
		OUTYY(("P(server_harden_short_bufsize:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->harden_short_bufsize = 
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_harden_large_queries: VAR_HARDEN_LARGE_QUERIES STRING_ARG
	{
		OUTYY(("P(server_harden_large_queries:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->harden_large_queries = 
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_harden_glue: VAR_HARDEN_GLUE STRING_ARG
	{
		OUTYY(("P(server_harden_glue:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->harden_glue = 
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_harden_dnssec_stripped: VAR_HARDEN_DNSSEC_STRIPPED STRING_ARG
	{
		OUTYY(("P(server_harden_dnssec_stripped:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->harden_dnssec_stripped = 
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_harden_below_nxdomain: VAR_HARDEN_BELOW_NXDOMAIN STRING_ARG
	{
		OUTYY(("P(server_harden_below_nxdomain:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->harden_below_nxdomain = 
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_harden_referral_path: VAR_HARDEN_REFERRAL_PATH STRING_ARG
	{
		OUTYY(("P(server_harden_referral_path:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->harden_referral_path = 
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_harden_algo_downgrade: VAR_HARDEN_ALGO_DOWNGRADE STRING_ARG
	{
		OUTYY(("P(server_harden_algo_downgrade:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->harden_algo_downgrade = 
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_use_caps_for_id: VAR_USE_CAPS_FOR_ID STRING_ARG
	{
		OUTYY(("P(server_use_caps_for_id:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->use_caps_bits_for_id = 
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_caps_whitelist: VAR_CAPS_WHITELIST STRING_ARG
	{
		OUTYY(("P(server_caps_whitelist:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->caps_whitelist, $2))
			yyerror("out of memory");
	}
	;
server_private_address: VAR_PRIVATE_ADDRESS STRING_ARG
	{
		OUTYY(("P(server_private_address:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->private_address, $2))
			yyerror("out of memory");
	}
	;
server_private_domain: VAR_PRIVATE_DOMAIN STRING_ARG
	{
		OUTYY(("P(server_private_domain:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->private_domain, $2))
			yyerror("out of memory");
	}
	;
server_prefetch: VAR_PREFETCH STRING_ARG
	{
		OUTYY(("P(server_prefetch:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->prefetch = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_prefetch_key: VAR_PREFETCH_KEY STRING_ARG
	{
		OUTYY(("P(server_prefetch_key:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->prefetch_key = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_unwanted_reply_threshold: VAR_UNWANTED_REPLY_THRESHOLD STRING_ARG
	{
		OUTYY(("P(server_unwanted_reply_threshold:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->unwanted_threshold = atoi($2);
		free($2);
	}
	;
server_do_not_query_address: VAR_DO_NOT_QUERY_ADDRESS STRING_ARG
	{
		OUTYY(("P(server_do_not_query_address:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->donotqueryaddrs, $2))
			yyerror("out of memory");
	}
	;
server_do_not_query_localhost: VAR_DO_NOT_QUERY_LOCALHOST STRING_ARG
	{
		OUTYY(("P(server_do_not_query_localhost:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->donotquery_localhost = 
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_access_control: VAR_ACCESS_CONTROL STRING_ARG STRING_ARG
	{
		OUTYY(("P(server_access_control:%s %s)\n", $2, $3));
		if(strcmp($3, "deny")!=0 && strcmp($3, "refuse")!=0 &&
			strcmp($3, "deny_non_local")!=0 &&
			strcmp($3, "refuse_non_local")!=0 &&
			strcmp($3, "allow_setrd")!=0 && 
			strcmp($3, "allow")!=0 && 
			strcmp($3, "allow_snoop")!=0) {
			yyerror("expected deny, refuse, deny_non_local, "
				"refuse_non_local, allow, allow_setrd or "
				"allow_snoop in access control action");
		} else {
			if(!cfg_str2list_insert(&cfg_parser->cfg->acls, $2, $3))
				fatal_exit("out of memory adding acl");
		}
	}
	;
server_module_conf: VAR_MODULE_CONF STRING_ARG
	{
		OUTYY(("P(server_module_conf:%s)\n", $2));
		free(cfg_parser->cfg->module_conf);
		cfg_parser->cfg->module_conf = $2;
	}
	;
server_val_override_date: VAR_VAL_OVERRIDE_DATE STRING_ARG
	{
		OUTYY(("P(server_val_override_date:%s)\n", $2));
		if(*$2 == '\0' || strcmp($2, "0") == 0) {
			cfg_parser->cfg->val_date_override = 0;
		} else if(strlen($2) == 14) {
			cfg_parser->cfg->val_date_override = 
				cfg_convert_timeval($2);
			if(!cfg_parser->cfg->val_date_override)
				yyerror("bad date/time specification");
		} else {
			if(atoi($2) == 0)
				yyerror("number expected");
			cfg_parser->cfg->val_date_override = atoi($2);
		}
		free($2);
	}
	;
server_val_sig_skew_min: VAR_VAL_SIG_SKEW_MIN STRING_ARG
	{
		OUTYY(("P(server_val_sig_skew_min:%s)\n", $2));
		if(*$2 == '\0' || strcmp($2, "0") == 0) {
			cfg_parser->cfg->val_sig_skew_min = 0;
		} else {
			cfg_parser->cfg->val_sig_skew_min = atoi($2);
			if(!cfg_parser->cfg->val_sig_skew_min)
				yyerror("number expected");
		}
		free($2);
	}
	;
server_val_sig_skew_max: VAR_VAL_SIG_SKEW_MAX STRING_ARG
	{
		OUTYY(("P(server_val_sig_skew_max:%s)\n", $2));
		if(*$2 == '\0' || strcmp($2, "0") == 0) {
			cfg_parser->cfg->val_sig_skew_max = 0;
		} else {
			cfg_parser->cfg->val_sig_skew_max = atoi($2);
			if(!cfg_parser->cfg->val_sig_skew_max)
				yyerror("number expected");
		}
		free($2);
	}
	;
server_cache_max_ttl: VAR_CACHE_MAX_TTL STRING_ARG
	{
		OUTYY(("P(server_cache_max_ttl:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->max_ttl = atoi($2);
		free($2);
	}
	;
server_cache_max_negative_ttl: VAR_CACHE_MAX_NEGATIVE_TTL STRING_ARG
	{
		OUTYY(("P(server_cache_max_negative_ttl:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->max_negative_ttl = atoi($2);
		free($2);
	}
	;
server_cache_min_ttl: VAR_CACHE_MIN_TTL STRING_ARG
	{
		OUTYY(("P(server_cache_min_ttl:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->min_ttl = atoi($2);
		free($2);
	}
	;
server_bogus_ttl: VAR_BOGUS_TTL STRING_ARG
	{
		OUTYY(("P(server_bogus_ttl:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->bogus_ttl = atoi($2);
		free($2);
	}
	;
server_val_clean_additional: VAR_VAL_CLEAN_ADDITIONAL STRING_ARG
	{
		OUTYY(("P(server_val_clean_additional:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->val_clean_additional = 
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_val_permissive_mode: VAR_VAL_PERMISSIVE_MODE STRING_ARG
	{
		OUTYY(("P(server_val_permissive_mode:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->val_permissive_mode = 
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_aggressive_nsec: VAR_AGGRESSIVE_NSEC STRING_ARG
	{
		OUTYY(("P(server_aggressive_nsec:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else
			cfg_parser->cfg->aggressive_nsec =
				(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_ignore_cd_flag: VAR_IGNORE_CD_FLAG STRING_ARG
	{
		OUTYY(("P(server_ignore_cd_flag:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->ignore_cd = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_serve_expired: VAR_SERVE_EXPIRED STRING_ARG
	{
		OUTYY(("P(server_serve_expired:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->serve_expired = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_serve_expired_ttl: VAR_SERVE_EXPIRED_TTL STRING_ARG
	{
		OUTYY(("P(server_serve_expired_ttl:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->serve_expired_ttl = atoi($2);
		free($2);
	}
	;
server_serve_expired_ttl_reset: VAR_SERVE_EXPIRED_TTL_RESET STRING_ARG
	{
		OUTYY(("P(server_serve_expired_ttl_reset:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->serve_expired_ttl_reset = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_fake_dsa: VAR_FAKE_DSA STRING_ARG
	{
		OUTYY(("P(server_fake_dsa:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
#ifdef HAVE_SSL
		else fake_dsa = (strcmp($2, "yes")==0);
		if(fake_dsa)
			log_warn("test option fake_dsa is enabled");
#endif
		free($2);
	}
	;
server_fake_sha1: VAR_FAKE_SHA1 STRING_ARG
	{
		OUTYY(("P(server_fake_sha1:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
#ifdef HAVE_SSL
		else fake_sha1 = (strcmp($2, "yes")==0);
		if(fake_sha1)
			log_warn("test option fake_sha1 is enabled");
#endif
		free($2);
	}
	;
server_val_log_level: VAR_VAL_LOG_LEVEL STRING_ARG
	{
		OUTYY(("P(server_val_log_level:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->val_log_level = atoi($2);
		free($2);
	}
	;
server_val_nsec3_keysize_iterations: VAR_VAL_NSEC3_KEYSIZE_ITERATIONS STRING_ARG
	{
		OUTYY(("P(server_val_nsec3_keysize_iterations:%s)\n", $2));
		free(cfg_parser->cfg->val_nsec3_key_iterations);
		cfg_parser->cfg->val_nsec3_key_iterations = $2;
	}
	;
server_add_holddown: VAR_ADD_HOLDDOWN STRING_ARG
	{
		OUTYY(("P(server_add_holddown:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->add_holddown = atoi($2);
		free($2);
	}
	;
server_del_holddown: VAR_DEL_HOLDDOWN STRING_ARG
	{
		OUTYY(("P(server_del_holddown:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->del_holddown = atoi($2);
		free($2);
	}
	;
server_keep_missing: VAR_KEEP_MISSING STRING_ARG
	{
		OUTYY(("P(server_keep_missing:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->keep_missing = atoi($2);
		free($2);
	}
	;
server_permit_small_holddown: VAR_PERMIT_SMALL_HOLDDOWN STRING_ARG
	{
		OUTYY(("P(server_permit_small_holddown:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->permit_small_holddown =
			(strcmp($2, "yes")==0);
		free($2);
	}
server_key_cache_size: VAR_KEY_CACHE_SIZE STRING_ARG
	{
		OUTYY(("P(server_key_cache_size:%s)\n", $2));
		if(!cfg_parse_memsize($2, &cfg_parser->cfg->key_cache_size))
			yyerror("memory size expected");
		free($2);
	}
	;
server_key_cache_slabs: VAR_KEY_CACHE_SLABS STRING_ARG
	{
		OUTYY(("P(server_key_cache_slabs:%s)\n", $2));
		if(atoi($2) == 0)
			yyerror("number expected");
		else {
			cfg_parser->cfg->key_cache_slabs = atoi($2);
			if(!is_pow2(cfg_parser->cfg->key_cache_slabs))
				yyerror("must be a power of 2");
		}
		free($2);
	}
	;
server_neg_cache_size: VAR_NEG_CACHE_SIZE STRING_ARG
	{
		OUTYY(("P(server_neg_cache_size:%s)\n", $2));
		if(!cfg_parse_memsize($2, &cfg_parser->cfg->neg_cache_size))
			yyerror("memory size expected");
		free($2);
	}
	;
server_local_zone: VAR_LOCAL_ZONE STRING_ARG STRING_ARG
	{
		OUTYY(("P(server_local_zone:%s %s)\n", $2, $3));
		if(strcmp($3, "static")!=0 && strcmp($3, "deny")!=0 &&
		   strcmp($3, "refuse")!=0 && strcmp($3, "redirect")!=0 &&
		   strcmp($3, "transparent")!=0 && strcmp($3, "nodefault")!=0
		   && strcmp($3, "typetransparent")!=0
		   && strcmp($3, "always_transparent")!=0
		   && strcmp($3, "always_refuse")!=0
		   && strcmp($3, "always_nxdomain")!=0
		   && strcmp($3, "noview")!=0
		   && strcmp($3, "inform")!=0 && strcmp($3, "inform_deny")!=0)
			yyerror("local-zone type: expected static, deny, "
				"refuse, redirect, transparent, "
				"typetransparent, inform, inform_deny, "
				"always_transparent, always_refuse, "
				"always_nxdomain, noview or nodefault");
		else if(strcmp($3, "nodefault")==0) {
			if(!cfg_strlist_insert(&cfg_parser->cfg->
				local_zones_nodefault, $2))
				fatal_exit("out of memory adding local-zone");
			free($3);
		} else {
			if(!cfg_str2list_insert(&cfg_parser->cfg->local_zones, 
				$2, $3))
				fatal_exit("out of memory adding local-zone");
		}
	}
	;
server_local_data: VAR_LOCAL_DATA STRING_ARG
	{
		OUTYY(("P(server_local_data:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->local_data, $2))
			fatal_exit("out of memory adding local-data");
	}
	;
server_local_data_ptr: VAR_LOCAL_DATA_PTR STRING_ARG
	{
		char* ptr;
		OUTYY(("P(server_local_data_ptr:%s)\n", $2));
		ptr = cfg_ptr_reverse($2);
		free($2);
		if(ptr) {
			if(!cfg_strlist_insert(&cfg_parser->cfg->
				local_data, ptr))
				fatal_exit("out of memory adding local-data");
		} else {
			yyerror("local-data-ptr could not be reversed");
		}
	}
	;
server_minimal_responses: VAR_MINIMAL_RESPONSES STRING_ARG
	{
		OUTYY(("P(server_minimal_responses:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->minimal_responses =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_rrset_roundrobin: VAR_RRSET_ROUNDROBIN STRING_ARG
	{
		OUTYY(("P(server_rrset_roundrobin:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->rrset_roundrobin =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_max_udp_size: VAR_MAX_UDP_SIZE STRING_ARG
	{
		OUTYY(("P(server_max_udp_size:%s)\n", $2));
		cfg_parser->cfg->max_udp_size = atoi($2);
		free($2);
	}
	;
server_dns64_prefix: VAR_DNS64_PREFIX STRING_ARG
	{
		OUTYY(("P(dns64_prefix:%s)\n", $2));
		free(cfg_parser->cfg->dns64_prefix);
		cfg_parser->cfg->dns64_prefix = $2;
	}
	;
server_dns64_synthall: VAR_DNS64_SYNTHALL STRING_ARG
	{
		OUTYY(("P(server_dns64_synthall:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->dns64_synthall = (strcmp($2, "yes")==0);
		free($2);
	}
	;
server_dns64_ignore_aaaa: VAR_DNS64_IGNORE_AAAA STRING_ARG
	{
		OUTYY(("P(dns64_ignore_aaaa:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->dns64_ignore_aaaa,
			$2))
			fatal_exit("out of memory adding dns64-ignore-aaaa");
	}
	;
server_define_tag: VAR_DEFINE_TAG STRING_ARG
	{
		char* p, *s = $2;
		OUTYY(("P(server_define_tag:%s)\n", $2));
		while((p=strsep(&s, " \t\n")) != NULL) {
			if(*p) {
				if(!config_add_tag(cfg_parser->cfg, p))
					yyerror("could not define-tag, "
						"out of memory");
			}
		}
		free($2);
	}
	;
server_local_zone_tag: VAR_LOCAL_ZONE_TAG STRING_ARG STRING_ARG
	{
		size_t len = 0;
		uint8_t* bitlist = config_parse_taglist(cfg_parser->cfg, $3,
			&len);
		free($3);
		OUTYY(("P(server_local_zone_tag:%s)\n", $2));
		if(!bitlist)
			yyerror("could not parse tags, (define-tag them first)");
		if(bitlist) {
			if(!cfg_strbytelist_insert(
				&cfg_parser->cfg->local_zone_tags,
				$2, bitlist, len)) {
				yyerror("out of memory");
				free($2);
			}
		}
	}
	;
server_access_control_tag: VAR_ACCESS_CONTROL_TAG STRING_ARG STRING_ARG
	{
		size_t len = 0;
		uint8_t* bitlist = config_parse_taglist(cfg_parser->cfg, $3,
			&len);
		free($3);
		OUTYY(("P(server_access_control_tag:%s)\n", $2));
		if(!bitlist)
			yyerror("could not parse tags, (define-tag them first)");
		if(bitlist) {
			if(!cfg_strbytelist_insert(
				&cfg_parser->cfg->acl_tags,
				$2, bitlist, len)) {
				yyerror("out of memory");
				free($2);
			}
		}
	}
	;
server_access_control_tag_action: VAR_ACCESS_CONTROL_TAG_ACTION STRING_ARG STRING_ARG STRING_ARG
	{
		OUTYY(("P(server_access_control_tag_action:%s %s %s)\n", $2, $3, $4));
		if(!cfg_str3list_insert(&cfg_parser->cfg->acl_tag_actions,
			$2, $3, $4)) {
			yyerror("out of memory");
			free($2);
			free($3);
			free($4);
		}
	}
	;
server_access_control_tag_data: VAR_ACCESS_CONTROL_TAG_DATA STRING_ARG STRING_ARG STRING_ARG
	{
		OUTYY(("P(server_access_control_tag_data:%s %s %s)\n", $2, $3, $4));
		if(!cfg_str3list_insert(&cfg_parser->cfg->acl_tag_datas,
			$2, $3, $4)) {
			yyerror("out of memory");
			free($2);
			free($3);
			free($4);
		}
	}
	;
server_local_zone_override: VAR_LOCAL_ZONE_OVERRIDE STRING_ARG STRING_ARG STRING_ARG
	{
		OUTYY(("P(server_local_zone_override:%s %s %s)\n", $2, $3, $4));
		if(!cfg_str3list_insert(&cfg_parser->cfg->local_zone_overrides,
			$2, $3, $4)) {
			yyerror("out of memory");
			free($2);
			free($3);
			free($4);
		}
	}
	;
server_access_control_view: VAR_ACCESS_CONTROL_VIEW STRING_ARG STRING_ARG
	{
		OUTYY(("P(server_access_control_view:%s %s)\n", $2, $3));
		if(!cfg_str2list_insert(&cfg_parser->cfg->acl_view,
			$2, $3)) {
			yyerror("out of memory");
			free($2);
			free($3);
		}
	}
	;
server_response_ip_tag: VAR_RESPONSE_IP_TAG STRING_ARG STRING_ARG
	{
		size_t len = 0;
		uint8_t* bitlist = config_parse_taglist(cfg_parser->cfg, $3,
			&len);
		free($3);
		OUTYY(("P(response_ip_tag:%s)\n", $2));
		if(!bitlist)
			yyerror("could not parse tags, (define-tag them first)");
		if(bitlist) {
			if(!cfg_strbytelist_insert(
				&cfg_parser->cfg->respip_tags,
				$2, bitlist, len)) {
				yyerror("out of memory");
				free($2);
			}
		}
	}
	;
server_ip_ratelimit: VAR_IP_RATELIMIT STRING_ARG 
	{ 
		OUTYY(("P(server_ip_ratelimit:%s)\n", $2)); 
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->ip_ratelimit = atoi($2);
		free($2);
	}
	;

server_ratelimit: VAR_RATELIMIT STRING_ARG 
	{ 
		OUTYY(("P(server_ratelimit:%s)\n", $2)); 
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->ratelimit = atoi($2);
		free($2);
	}
	;
server_ip_ratelimit_size: VAR_IP_RATELIMIT_SIZE STRING_ARG
  {
  	OUTYY(("P(server_ip_ratelimit_size:%s)\n", $2));
  	if(!cfg_parse_memsize($2, &cfg_parser->cfg->ip_ratelimit_size))
  		yyerror("memory size expected");
  	free($2);
  }
  ;
server_ratelimit_size: VAR_RATELIMIT_SIZE STRING_ARG
	{
		OUTYY(("P(server_ratelimit_size:%s)\n", $2));
		if(!cfg_parse_memsize($2, &cfg_parser->cfg->ratelimit_size))
			yyerror("memory size expected");
		free($2);
	}
	;
server_ip_ratelimit_slabs: VAR_IP_RATELIMIT_SLABS STRING_ARG
  {
  	OUTYY(("P(server_ip_ratelimit_slabs:%s)\n", $2));
  	if(atoi($2) == 0)
  		yyerror("number expected");
  	else {
  		cfg_parser->cfg->ip_ratelimit_slabs = atoi($2);
  		if(!is_pow2(cfg_parser->cfg->ip_ratelimit_slabs))
  			yyerror("must be a power of 2");
  	}
  	free($2);
  }
  ;
server_ratelimit_slabs: VAR_RATELIMIT_SLABS STRING_ARG
	{
		OUTYY(("P(server_ratelimit_slabs:%s)\n", $2));
		if(atoi($2) == 0)
			yyerror("number expected");
		else {
			cfg_parser->cfg->ratelimit_slabs = atoi($2);
			if(!is_pow2(cfg_parser->cfg->ratelimit_slabs))
				yyerror("must be a power of 2");
		}
		free($2);
	}
	;
server_ratelimit_for_domain: VAR_RATELIMIT_FOR_DOMAIN STRING_ARG STRING_ARG
	{
		OUTYY(("P(server_ratelimit_for_domain:%s %s)\n", $2, $3));
		if(atoi($3) == 0 && strcmp($3, "0") != 0) {
			yyerror("number expected");
		} else {
			if(!cfg_str2list_insert(&cfg_parser->cfg->
				ratelimit_for_domain, $2, $3))
				fatal_exit("out of memory adding "
					"ratelimit-for-domain");
		}
	}
	;
server_ratelimit_below_domain: VAR_RATELIMIT_BELOW_DOMAIN STRING_ARG STRING_ARG
	{
		OUTYY(("P(server_ratelimit_below_domain:%s %s)\n", $2, $3));
		if(atoi($3) == 0 && strcmp($3, "0") != 0) {
			yyerror("number expected");
		} else {
			if(!cfg_str2list_insert(&cfg_parser->cfg->
				ratelimit_below_domain, $2, $3))
				fatal_exit("out of memory adding "
					"ratelimit-below-domain");
		}
	}
	;
server_ip_ratelimit_factor: VAR_IP_RATELIMIT_FACTOR STRING_ARG 
  { 
  	OUTYY(("P(server_ip_ratelimit_factor:%s)\n", $2)); 
  	if(atoi($2) == 0 && strcmp($2, "0") != 0)
  		yyerror("number expected");
  	else cfg_parser->cfg->ip_ratelimit_factor = atoi($2);
  	free($2);
	}
	;
server_ratelimit_factor: VAR_RATELIMIT_FACTOR STRING_ARG 
	{ 
		OUTYY(("P(server_ratelimit_factor:%s)\n", $2)); 
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->ratelimit_factor = atoi($2);
		free($2);
	}
	;
server_low_rtt: VAR_LOW_RTT STRING_ARG 
	{ 
		OUTYY(("P(server_low_rtt:%s)\n", $2)); 
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->low_rtt = atoi($2);
		free($2);
	}
	;
server_low_rtt_permil: VAR_LOW_RTT_PERMIL STRING_ARG 
	{ 
		OUTYY(("P(server_low_rtt_permil:%s)\n", $2)); 
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->low_rtt_permil = atoi($2);
		free($2);
	}
	;
server_qname_minimisation: VAR_QNAME_MINIMISATION STRING_ARG
	{
		OUTYY(("P(server_qname_minimisation:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->qname_minimisation = 
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_qname_minimisation_strict: VAR_QNAME_MINIMISATION_STRICT STRING_ARG
	{
		OUTYY(("P(server_qname_minimisation_strict:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->qname_minimisation_strict = 
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_ipsecmod_enabled: VAR_IPSECMOD_ENABLED STRING_ARG
	{
	#ifdef USE_IPSECMOD
		OUTYY(("P(server_ipsecmod_enabled:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->ipsecmod_enabled = (strcmp($2, "yes")==0);
		free($2);
	#else
		OUTYY(("P(Compiled without IPsec module, ignoring)\n"));
	#endif
	}
	;
server_ipsecmod_ignore_bogus: VAR_IPSECMOD_IGNORE_BOGUS STRING_ARG
	{
	#ifdef USE_IPSECMOD
		OUTYY(("P(server_ipsecmod_ignore_bogus:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->ipsecmod_ignore_bogus = (strcmp($2, "yes")==0);
		free($2);
	#else
		OUTYY(("P(Compiled without IPsec module, ignoring)\n"));
	#endif
	}
	;
server_ipsecmod_hook: VAR_IPSECMOD_HOOK STRING_ARG
	{
	#ifdef USE_IPSECMOD
		OUTYY(("P(server_ipsecmod_hook:%s)\n", $2));
		free(cfg_parser->cfg->ipsecmod_hook);
		cfg_parser->cfg->ipsecmod_hook = $2;
	#else
		OUTYY(("P(Compiled without IPsec module, ignoring)\n"));
	#endif
	}
	;
server_ipsecmod_max_ttl: VAR_IPSECMOD_MAX_TTL STRING_ARG
	{
	#ifdef USE_IPSECMOD
		OUTYY(("P(server_ipsecmod_max_ttl:%s)\n", $2));
		if(atoi($2) == 0 && strcmp($2, "0") != 0)
			yyerror("number expected");
		else cfg_parser->cfg->ipsecmod_max_ttl = atoi($2);
		free($2);
	#else
		OUTYY(("P(Compiled without IPsec module, ignoring)\n"));
	#endif
	}
	;
server_ipsecmod_whitelist: VAR_IPSECMOD_WHITELIST STRING_ARG
	{
	#ifdef USE_IPSECMOD
		OUTYY(("P(server_ipsecmod_whitelist:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->ipsecmod_whitelist, $2))
			yyerror("out of memory");
	#else
		OUTYY(("P(Compiled without IPsec module, ignoring)\n"));
	#endif
	}
	;
server_ipsecmod_strict: VAR_IPSECMOD_STRICT STRING_ARG
	{
	#ifdef USE_IPSECMOD
		OUTYY(("P(server_ipsecmod_strict:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->ipsecmod_strict = (strcmp($2, "yes")==0);
		free($2);
	#else
		OUTYY(("P(Compiled without IPsec module, ignoring)\n"));
	#endif
	}
	;
stub_name: VAR_NAME STRING_ARG
	{
		OUTYY(("P(name:%s)\n", $2));
		if(cfg_parser->cfg->stubs->name)
			yyerror("stub name override, there must be one name "
				"for one stub-zone");
		free(cfg_parser->cfg->stubs->name);
		cfg_parser->cfg->stubs->name = $2;
	}
	;
stub_host: VAR_STUB_HOST STRING_ARG
	{
		OUTYY(("P(stub-host:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->stubs->hosts, $2))
			yyerror("out of memory");
	}
	;
stub_addr: VAR_STUB_ADDR STRING_ARG
	{
		OUTYY(("P(stub-addr:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->stubs->addrs, $2))
			yyerror("out of memory");
	}
	;
stub_first: VAR_STUB_FIRST STRING_ARG
	{
		OUTYY(("P(stub-first:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->stubs->isfirst=(strcmp($2, "yes")==0);
		free($2);
	}
	;
stub_no_cache: VAR_STUB_NO_CACHE STRING_ARG
	{
		OUTYY(("P(stub-no-cache:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->stubs->no_cache=(strcmp($2, "yes")==0);
		free($2);
	}
	;
stub_ssl_upstream: VAR_STUB_SSL_UPSTREAM STRING_ARG
	{
		OUTYY(("P(stub-ssl-upstream:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->stubs->ssl_upstream = 
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
stub_prime: VAR_STUB_PRIME STRING_ARG
	{
		OUTYY(("P(stub-prime:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->stubs->isprime = 
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
forward_name: VAR_NAME STRING_ARG
	{
		OUTYY(("P(name:%s)\n", $2));
		if(cfg_parser->cfg->forwards->name)
			yyerror("forward name override, there must be one "
				"name for one forward-zone");
		free(cfg_parser->cfg->forwards->name);
		cfg_parser->cfg->forwards->name = $2;
	}
	;
forward_host: VAR_FORWARD_HOST STRING_ARG
	{
		OUTYY(("P(forward-host:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->forwards->hosts, $2))
			yyerror("out of memory");
	}
	;
forward_addr: VAR_FORWARD_ADDR STRING_ARG
	{
		OUTYY(("P(forward-addr:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->forwards->addrs, $2))
			yyerror("out of memory");
	}
	;
forward_first: VAR_FORWARD_FIRST STRING_ARG
	{
		OUTYY(("P(forward-first:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->forwards->isfirst=(strcmp($2, "yes")==0);
		free($2);
	}
	;
forward_no_cache: VAR_FORWARD_NO_CACHE STRING_ARG
	{
		OUTYY(("P(forward-no-cache:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->forwards->no_cache=(strcmp($2, "yes")==0);
		free($2);
	}
	;
forward_ssl_upstream: VAR_FORWARD_SSL_UPSTREAM STRING_ARG
	{
		OUTYY(("P(forward-ssl-upstream:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->forwards->ssl_upstream = 
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
auth_name: VAR_NAME STRING_ARG
	{
		OUTYY(("P(name:%s)\n", $2));
		if(cfg_parser->cfg->auths->name)
			yyerror("auth name override, there must be one name "
				"for one auth-zone");
		free(cfg_parser->cfg->auths->name);
		cfg_parser->cfg->auths->name = $2;
	}
	;
auth_zonefile: VAR_ZONEFILE STRING_ARG
	{
		OUTYY(("P(zonefile:%s)\n", $2));
		free(cfg_parser->cfg->auths->zonefile);
		cfg_parser->cfg->auths->zonefile = $2;
	}
	;
auth_master: VAR_MASTER STRING_ARG
	{
		OUTYY(("P(master:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->auths->masters, $2))
			yyerror("out of memory");
	}
	;
auth_url: VAR_URL STRING_ARG
	{
		OUTYY(("P(url:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->auths->urls, $2))
			yyerror("out of memory");
	}
	;
auth_allow_notify: VAR_ALLOW_NOTIFY STRING_ARG
	{
		OUTYY(("P(allow-notify:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->auths->allow_notify,
			$2))
			yyerror("out of memory");
	}
	;
auth_for_downstream: VAR_FOR_DOWNSTREAM STRING_ARG
	{
		OUTYY(("P(for-downstream:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->auths->for_downstream =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
auth_for_upstream: VAR_FOR_UPSTREAM STRING_ARG
	{
		OUTYY(("P(for-upstream:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->auths->for_upstream =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
auth_fallback_enabled: VAR_FALLBACK_ENABLED STRING_ARG
	{
		OUTYY(("P(fallback-enabled:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->auths->fallback_enabled =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
view_name: VAR_NAME STRING_ARG
	{
		OUTYY(("P(name:%s)\n", $2));
		if(cfg_parser->cfg->views->name)
			yyerror("view name override, there must be one "
				"name for one view");
		free(cfg_parser->cfg->views->name);
		cfg_parser->cfg->views->name = $2;
	}
	;
view_local_zone: VAR_LOCAL_ZONE STRING_ARG STRING_ARG
	{
		OUTYY(("P(view_local_zone:%s %s)\n", $2, $3));
		if(strcmp($3, "static")!=0 && strcmp($3, "deny")!=0 &&
		   strcmp($3, "refuse")!=0 && strcmp($3, "redirect")!=0 &&
		   strcmp($3, "transparent")!=0 && strcmp($3, "nodefault")!=0
		   && strcmp($3, "typetransparent")!=0
		   && strcmp($3, "always_transparent")!=0
		   && strcmp($3, "always_refuse")!=0
		   && strcmp($3, "always_nxdomain")!=0
		   && strcmp($3, "noview")!=0
		   && strcmp($3, "inform")!=0 && strcmp($3, "inform_deny")!=0)
			yyerror("local-zone type: expected static, deny, "
				"refuse, redirect, transparent, "
				"typetransparent, inform, inform_deny, "
				"always_transparent, always_refuse, "
				"always_nxdomain, noview or nodefault");
		else if(strcmp($3, "nodefault")==0) {
			if(!cfg_strlist_insert(&cfg_parser->cfg->views->
				local_zones_nodefault, $2))
				fatal_exit("out of memory adding local-zone");
			free($3);
		} else {
			if(!cfg_str2list_insert(
				&cfg_parser->cfg->views->local_zones, 
				$2, $3))
				fatal_exit("out of memory adding local-zone");
		}
	}
	;
view_response_ip: VAR_RESPONSE_IP STRING_ARG STRING_ARG
	{
		OUTYY(("P(view_response_ip:%s %s)\n", $2, $3));
		validate_respip_action($3);
		if(!cfg_str2list_insert(
			&cfg_parser->cfg->views->respip_actions, $2, $3))
			fatal_exit("out of memory adding per-view "
				"response-ip action");
	}
	;
view_response_ip_data: VAR_RESPONSE_IP_DATA STRING_ARG STRING_ARG
	{
		OUTYY(("P(view_response_ip_data:%s)\n", $2));
		if(!cfg_str2list_insert(
			&cfg_parser->cfg->views->respip_data, $2, $3))
			fatal_exit("out of memory adding response-ip-data");
	}
	;
view_local_data: VAR_LOCAL_DATA STRING_ARG
	{
		OUTYY(("P(view_local_data:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->views->local_data, $2)) {
			fatal_exit("out of memory adding local-data");
			free($2);
		}
	}
	;
view_local_data_ptr: VAR_LOCAL_DATA_PTR STRING_ARG
	{
		char* ptr;
		OUTYY(("P(view_local_data_ptr:%s)\n", $2));
		ptr = cfg_ptr_reverse($2);
		free($2);
		if(ptr) {
			if(!cfg_strlist_insert(&cfg_parser->cfg->views->
				local_data, ptr))
				fatal_exit("out of memory adding local-data");
		} else {
			yyerror("local-data-ptr could not be reversed");
		}
	}
	;
view_first: VAR_VIEW_FIRST STRING_ARG
	{
		OUTYY(("P(view-first:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->views->isfirst=(strcmp($2, "yes")==0);
		free($2);
	}
	;
rcstart: VAR_REMOTE_CONTROL
	{ 
		OUTYY(("\nP(remote-control:)\n")); 
	}
	;
contents_rc: contents_rc content_rc 
	| ;
content_rc: rc_control_enable | rc_control_interface | rc_control_port |
	rc_server_key_file | rc_server_cert_file | rc_control_key_file |
	rc_control_cert_file | rc_control_use_cert
	;
rc_control_enable: VAR_CONTROL_ENABLE STRING_ARG
	{
		OUTYY(("P(control_enable:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->remote_control_enable = 
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
rc_control_port: VAR_CONTROL_PORT STRING_ARG
	{
		OUTYY(("P(control_port:%s)\n", $2));
		if(atoi($2) == 0)
			yyerror("control port number expected");
		else cfg_parser->cfg->control_port = atoi($2);
		free($2);
	}
	;
rc_control_interface: VAR_CONTROL_INTERFACE STRING_ARG
	{
		OUTYY(("P(control_interface:%s)\n", $2));
		if(!cfg_strlist_append(&cfg_parser->cfg->control_ifs, $2))
			yyerror("out of memory");
	}
	;
rc_control_use_cert: VAR_CONTROL_USE_CERT STRING_ARG
	{
		OUTYY(("P(control_use_cert:%s)\n", $2));
		cfg_parser->cfg->control_use_cert = (strcmp($2, "yes")==0);
		free($2);
	}
	;
rc_server_key_file: VAR_SERVER_KEY_FILE STRING_ARG
	{
		OUTYY(("P(rc_server_key_file:%s)\n", $2));
		free(cfg_parser->cfg->server_key_file);
		cfg_parser->cfg->server_key_file = $2;
	}
	;
rc_server_cert_file: VAR_SERVER_CERT_FILE STRING_ARG
	{
		OUTYY(("P(rc_server_cert_file:%s)\n", $2));
		free(cfg_parser->cfg->server_cert_file);
		cfg_parser->cfg->server_cert_file = $2;
	}
	;
rc_control_key_file: VAR_CONTROL_KEY_FILE STRING_ARG
	{
		OUTYY(("P(rc_control_key_file:%s)\n", $2));
		free(cfg_parser->cfg->control_key_file);
		cfg_parser->cfg->control_key_file = $2;
	}
	;
rc_control_cert_file: VAR_CONTROL_CERT_FILE STRING_ARG
	{
		OUTYY(("P(rc_control_cert_file:%s)\n", $2));
		free(cfg_parser->cfg->control_cert_file);
		cfg_parser->cfg->control_cert_file = $2;
	}
	;
dtstart: VAR_DNSTAP
	{
		OUTYY(("\nP(dnstap:)\n"));
	}
	;
contents_dt: contents_dt content_dt
	| ;
content_dt: dt_dnstap_enable | dt_dnstap_socket_path |
	dt_dnstap_send_identity | dt_dnstap_send_version |
	dt_dnstap_identity | dt_dnstap_version |
	dt_dnstap_log_resolver_query_messages |
	dt_dnstap_log_resolver_response_messages |
	dt_dnstap_log_client_query_messages |
	dt_dnstap_log_client_response_messages |
	dt_dnstap_log_forwarder_query_messages |
	dt_dnstap_log_forwarder_response_messages
	;
dt_dnstap_enable: VAR_DNSTAP_ENABLE STRING_ARG
	{
		OUTYY(("P(dt_dnstap_enable:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->dnstap = (strcmp($2, "yes")==0);
	}
	;
dt_dnstap_socket_path: VAR_DNSTAP_SOCKET_PATH STRING_ARG
	{
		OUTYY(("P(dt_dnstap_socket_path:%s)\n", $2));
		free(cfg_parser->cfg->dnstap_socket_path);
		cfg_parser->cfg->dnstap_socket_path = $2;
	}
	;
dt_dnstap_send_identity: VAR_DNSTAP_SEND_IDENTITY STRING_ARG
	{
		OUTYY(("P(dt_dnstap_send_identity:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->dnstap_send_identity = (strcmp($2, "yes")==0);
	}
	;
dt_dnstap_send_version: VAR_DNSTAP_SEND_VERSION STRING_ARG
	{
		OUTYY(("P(dt_dnstap_send_version:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->dnstap_send_version = (strcmp($2, "yes")==0);
	}
	;
dt_dnstap_identity: VAR_DNSTAP_IDENTITY STRING_ARG
	{
		OUTYY(("P(dt_dnstap_identity:%s)\n", $2));
		free(cfg_parser->cfg->dnstap_identity);
		cfg_parser->cfg->dnstap_identity = $2;
	}
	;
dt_dnstap_version: VAR_DNSTAP_VERSION STRING_ARG
	{
		OUTYY(("P(dt_dnstap_version:%s)\n", $2));
		free(cfg_parser->cfg->dnstap_version);
		cfg_parser->cfg->dnstap_version = $2;
	}
	;
dt_dnstap_log_resolver_query_messages: VAR_DNSTAP_LOG_RESOLVER_QUERY_MESSAGES STRING_ARG
	{
		OUTYY(("P(dt_dnstap_log_resolver_query_messages:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->dnstap_log_resolver_query_messages =
			(strcmp($2, "yes")==0);
	}
	;
dt_dnstap_log_resolver_response_messages: VAR_DNSTAP_LOG_RESOLVER_RESPONSE_MESSAGES STRING_ARG
	{
		OUTYY(("P(dt_dnstap_log_resolver_response_messages:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->dnstap_log_resolver_response_messages =
			(strcmp($2, "yes")==0);
	}
	;
dt_dnstap_log_client_query_messages: VAR_DNSTAP_LOG_CLIENT_QUERY_MESSAGES STRING_ARG
	{
		OUTYY(("P(dt_dnstap_log_client_query_messages:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->dnstap_log_client_query_messages =
			(strcmp($2, "yes")==0);
	}
	;
dt_dnstap_log_client_response_messages: VAR_DNSTAP_LOG_CLIENT_RESPONSE_MESSAGES STRING_ARG
	{
		OUTYY(("P(dt_dnstap_log_client_response_messages:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->dnstap_log_client_response_messages =
			(strcmp($2, "yes")==0);
	}
	;
dt_dnstap_log_forwarder_query_messages: VAR_DNSTAP_LOG_FORWARDER_QUERY_MESSAGES STRING_ARG
	{
		OUTYY(("P(dt_dnstap_log_forwarder_query_messages:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->dnstap_log_forwarder_query_messages =
			(strcmp($2, "yes")==0);
	}
	;
dt_dnstap_log_forwarder_response_messages: VAR_DNSTAP_LOG_FORWARDER_RESPONSE_MESSAGES STRING_ARG
	{
		OUTYY(("P(dt_dnstap_log_forwarder_response_messages:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->dnstap_log_forwarder_response_messages =
			(strcmp($2, "yes")==0);
	}
	;
pythonstart: VAR_PYTHON
	{ 
		OUTYY(("\nP(python:)\n")); 
	}
	;
contents_py: contents_py content_py
	| ;
content_py: py_script
	;
py_script: VAR_PYTHON_SCRIPT STRING_ARG
	{
		OUTYY(("P(python-script:%s)\n", $2));
		free(cfg_parser->cfg->python_script);
		cfg_parser->cfg->python_script = $2;
	}
server_disable_dnssec_lame_check: VAR_DISABLE_DNSSEC_LAME_CHECK STRING_ARG
	{
		OUTYY(("P(disable_dnssec_lame_check:%s)\n", $2));
		if (strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->disable_dnssec_lame_check =
			(strcmp($2, "yes")==0);
		free($2);
	}
	;
server_log_identity: VAR_LOG_IDENTITY STRING_ARG
	{
		OUTYY(("P(server_log_identity:%s)\n", $2));
		free(cfg_parser->cfg->log_identity);
		cfg_parser->cfg->log_identity = $2;
	}
	;
server_response_ip: VAR_RESPONSE_IP STRING_ARG STRING_ARG
	{
		OUTYY(("P(server_response_ip:%s %s)\n", $2, $3));
		validate_respip_action($3);
		if(!cfg_str2list_insert(&cfg_parser->cfg->respip_actions,
			$2, $3))
			fatal_exit("out of memory adding response-ip");
	}
	;
server_response_ip_data: VAR_RESPONSE_IP_DATA STRING_ARG STRING_ARG
	{
		OUTYY(("P(server_response_ip_data:%s)\n", $2));
			if(!cfg_str2list_insert(&cfg_parser->cfg->respip_data,
				$2, $3))
				fatal_exit("out of memory adding response-ip-data");
	}
	;
dnscstart: VAR_DNSCRYPT
	{
		OUTYY(("\nP(dnscrypt:)\n"));
		OUTYY(("\nP(dnscrypt:)\n"));
	}
	;
contents_dnsc: contents_dnsc content_dnsc
	| ;
content_dnsc:
	dnsc_dnscrypt_enable | dnsc_dnscrypt_port | dnsc_dnscrypt_provider |
	dnsc_dnscrypt_secret_key | dnsc_dnscrypt_provider_cert |
	dnsc_dnscrypt_provider_cert_rotated |
	dnsc_dnscrypt_shared_secret_cache_size |
	dnsc_dnscrypt_shared_secret_cache_slabs |
	dnsc_dnscrypt_nonce_cache_size |
	dnsc_dnscrypt_nonce_cache_slabs
	;
dnsc_dnscrypt_enable: VAR_DNSCRYPT_ENABLE STRING_ARG
	{
		OUTYY(("P(dnsc_dnscrypt_enable:%s)\n", $2));
		if(strcmp($2, "yes") != 0 && strcmp($2, "no") != 0)
			yyerror("expected yes or no.");
		else cfg_parser->cfg->dnscrypt = (strcmp($2, "yes")==0);
		free($2);
	}
	;

dnsc_dnscrypt_port: VAR_DNSCRYPT_PORT STRING_ARG
	{
		OUTYY(("P(dnsc_dnscrypt_port:%s)\n", $2));

		if(atoi($2) == 0)
			yyerror("port number expected");
		else cfg_parser->cfg->dnscrypt_port = atoi($2);
		free($2);
	}
	;
dnsc_dnscrypt_provider: VAR_DNSCRYPT_PROVIDER STRING_ARG
	{
		OUTYY(("P(dnsc_dnscrypt_provider:%s)\n", $2));
		free(cfg_parser->cfg->dnscrypt_provider);
		cfg_parser->cfg->dnscrypt_provider = $2;
	}
	;
dnsc_dnscrypt_provider_cert: VAR_DNSCRYPT_PROVIDER_CERT STRING_ARG
	{
		OUTYY(("P(dnsc_dnscrypt_provider_cert:%s)\n", $2));
		if(cfg_strlist_find(cfg_parser->cfg->dnscrypt_provider_cert, $2))
			log_warn("dnscrypt-provider-cert %s is a duplicate", $2);
		if(!cfg_strlist_insert(&cfg_parser->cfg->dnscrypt_provider_cert, $2))
			fatal_exit("out of memory adding dnscrypt-provider-cert");
	}
	;
dnsc_dnscrypt_provider_cert_rotated: VAR_DNSCRYPT_PROVIDER_CERT_ROTATED STRING_ARG
	{
		OUTYY(("P(dnsc_dnscrypt_provider_cert_rotated:%s)\n", $2));
		if(!cfg_strlist_insert(&cfg_parser->cfg->dnscrypt_provider_cert_rotated, $2))
			fatal_exit("out of memory adding dnscrypt-provider-cert-rotated");
	}
	;
dnsc_dnscrypt_secret_key: VAR_DNSCRYPT_SECRET_KEY STRING_ARG
	{
		OUTYY(("P(dnsc_dnscrypt_secret_key:%s)\n", $2));
		if(cfg_strlist_find(cfg_parser->cfg->dnscrypt_secret_key, $2))
			log_warn("dnscrypt-secret-key: %s is a duplicate", $2);
		if(!cfg_strlist_insert(&cfg_parser->cfg->dnscrypt_secret_key, $2))
			fatal_exit("out of memory adding dnscrypt-secret-key");
	}
	;
dnsc_dnscrypt_shared_secret_cache_size: VAR_DNSCRYPT_SHARED_SECRET_CACHE_SIZE STRING_ARG
  {
  	OUTYY(("P(dnscrypt_shared_secret_cache_size:%s)\n", $2));
  	if(!cfg_parse_memsize($2, &cfg_parser->cfg->dnscrypt_shared_secret_cache_size))
  		yyerror("memory size expected");
  	free($2);
  }
  ;
dnsc_dnscrypt_shared_secret_cache_slabs: VAR_DNSCRYPT_SHARED_SECRET_CACHE_SLABS STRING_ARG
  {
  	OUTYY(("P(dnscrypt_shared_secret_cache_slabs:%s)\n", $2));
  	if(atoi($2) == 0)
  		yyerror("number expected");
  	else {
  		cfg_parser->cfg->dnscrypt_shared_secret_cache_slabs = atoi($2);
  		if(!is_pow2(cfg_parser->cfg->dnscrypt_shared_secret_cache_slabs))
  			yyerror("must be a power of 2");
  	}
  	free($2);
  }
  ;
dnsc_dnscrypt_nonce_cache_size: VAR_DNSCRYPT_NONCE_CACHE_SIZE STRING_ARG
  {
  	OUTYY(("P(dnscrypt_nonce_cache_size:%s)\n", $2));
  	if(!cfg_parse_memsize($2, &cfg_parser->cfg->dnscrypt_nonce_cache_size))
  		yyerror("memory size expected");
  	free($2);
  }
  ;
dnsc_dnscrypt_nonce_cache_slabs: VAR_DNSCRYPT_NONCE_CACHE_SLABS STRING_ARG
  {
  	OUTYY(("P(dnscrypt_nonce_cache_slabs:%s)\n", $2));
  	if(atoi($2) == 0)
  		yyerror("number expected");
  	else {
  		cfg_parser->cfg->dnscrypt_nonce_cache_slabs = atoi($2);
  		if(!is_pow2(cfg_parser->cfg->dnscrypt_nonce_cache_slabs))
  			yyerror("must be a power of 2");
  	}
  	free($2);
  }
  ;
cachedbstart: VAR_CACHEDB
	{
		OUTYY(("\nP(cachedb:)\n"));
	}
	;
contents_cachedb: contents_cachedb content_cachedb
	| ;
content_cachedb: cachedb_backend_name | cachedb_secret_seed |
	redis_server_host | redis_server_port | redis_timeout
	;
cachedb_backend_name: VAR_CACHEDB_BACKEND STRING_ARG
	{
	#ifdef USE_CACHEDB
		OUTYY(("P(backend:%s)\n", $2));
		if(cfg_parser->cfg->cachedb_backend)
			yyerror("cachedb backend override, there must be one "
				"backend");
		free(cfg_parser->cfg->cachedb_backend);
		cfg_parser->cfg->cachedb_backend = $2;
	#else
		OUTYY(("P(Compiled without cachedb, ignoring)\n"));
	#endif
	}
	;
cachedb_secret_seed: VAR_CACHEDB_SECRETSEED STRING_ARG
	{
	#ifdef USE_CACHEDB
		OUTYY(("P(secret-seed:%s)\n", $2));
		if(cfg_parser->cfg->cachedb_secret)
			yyerror("cachedb secret-seed override, there must be "
				"only one secret");
		free(cfg_parser->cfg->cachedb_secret);
		cfg_parser->cfg->cachedb_secret = $2;
	#else
		OUTYY(("P(Compiled without cachedb, ignoring)\n"));
		free($2);
	#endif
	}
	;
redis_server_host: VAR_CACHEDB_REDISHOST STRING_ARG
	{
	#if defined(USE_CACHEDB) && defined(USE_REDIS)
		OUTYY(("P(redis_server_host:%s)\n", $2));
		free(cfg_parser->cfg->redis_server_host);
		cfg_parser->cfg->redis_server_host = $2;
	#else
		OUTYY(("P(Compiled without cachedb or redis, ignoring)\n"));
		free($2);
	#endif
	}
	;
redis_server_port: VAR_CACHEDB_REDISPORT STRING_ARG
	{
	#if defined(USE_CACHEDB) && defined(USE_REDIS)
		int port;
		OUTYY(("P(redis_server_port:%s)\n", $2));
		port = atoi($2);
		if(port == 0 || port < 0 || port > 65535)
			yyerror("valid redis server port number expected");
		else cfg_parser->cfg->redis_server_port = port;
	#else
		OUTYY(("P(Compiled without cachedb or redis, ignoring)\n"));
	#endif
		free($2);
	}
	;
redis_timeout: VAR_CACHEDB_REDISTIMEOUT STRING_ARG
	{
	#if defined(USE_CACHEDB) && defined(USE_REDIS)
		OUTYY(("P(redis_timeout:%s)\n", $2));
		if(atoi($2) == 0)
			yyerror("redis timeout value expected");
		else cfg_parser->cfg->redis_timeout = atoi($2);
	#else
		OUTYY(("P(Compiled without cachedb or redis, ignoring)\n"));
	#endif
		free($2);
	}
	;
server_tcp_connection_limit: VAR_TCP_CONNECTION_LIMIT STRING_ARG STRING_ARG
	{
		OUTYY(("P(server_tcp_connection_limit:%s %s)\n", $2, $3));
		if (atoi($3) < 0)
			yyerror("positive number expected");
		else {
			if(!cfg_str2list_insert(&cfg_parser->cfg->tcp_connection_limits, $2, $3))
				fatal_exit("out of memory adding tcp connection limit");
		}
	}
	;
%%

/* parse helper routines could be here */
static void
validate_respip_action(const char* action)
{
	if(strcmp(action, "deny")!=0 &&
		strcmp(action, "redirect")!=0 &&
		strcmp(action, "inform")!=0 &&
		strcmp(action, "inform_deny")!=0 &&
		strcmp(action, "always_transparent")!=0 &&
		strcmp(action, "always_refuse")!=0 &&
		strcmp(action, "always_nxdomain")!=0)
	{
		yyerror("response-ip action: expected deny, redirect, "
			"inform, inform_deny, always_transparent, "
			"always_refuse or always_nxdomain");
	}
}
