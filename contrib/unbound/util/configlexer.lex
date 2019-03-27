%{
/*
 * configlexer.lex - lexical analyzer for unbound config file
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved
 *
 * See LICENSE for the license.
 *
 */
#include "config.h"

/* because flex keeps having sign-unsigned compare problems that are unfixed*/
#if defined(__clang__)||(defined(__GNUC__)&&((__GNUC__ >4)||(defined(__GNUC_MINOR__)&&(__GNUC__ ==4)&&(__GNUC_MINOR__ >=2))))
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif

#include <ctype.h>
#include <strings.h>
#ifdef HAVE_GLOB_H
# include <glob.h>
#endif

#include "util/config_file.h"
#include "configparser.h"
void ub_c_error(const char *message);

#if 0
#define LEXOUT(s)  printf s /* used ONLY when debugging */
#else
#define LEXOUT(s)
#endif

/** avoid warning in about fwrite return value */
#define ECHO ub_c_error_msg("syntax error at text: %s", ub_c_text)

/** A parser variable, this is a statement in the config file which is
 * of the form variable: value1 value2 ...  nargs is the number of values. */
#define YDVAR(nargs, var) \
	num_args=(nargs); \
	LEXOUT(("v(%s%d) ", ub_c_text, num_args)); \
	if(num_args > 0) { BEGIN(val); } \
	return (var);

struct inc_state {
	char* filename;
	int line;
	YY_BUFFER_STATE buffer;
	struct inc_state* next;
};
static struct inc_state* config_include_stack = NULL;
static int inc_depth = 0;
static int inc_prev = 0;
static int num_args = 0;

void init_cfg_parse(void)
{
	config_include_stack = NULL;
	inc_depth = 0;
	inc_prev = 0;
	num_args = 0;
}

static void config_start_include(const char* filename)
{
	FILE *input;
	struct inc_state* s;
	char* nm;
	if(inc_depth++ > 100000) {
		ub_c_error_msg("too many include files");
		return;
	}
	if(*filename == '\0') {
		ub_c_error_msg("empty include file name");
		return;
	}
	s = (struct inc_state*)malloc(sizeof(*s));
	if(!s) {
		ub_c_error_msg("include %s: malloc failure", filename);
		return;
	}
	if(cfg_parser->chroot && strncmp(filename, cfg_parser->chroot,
		strlen(cfg_parser->chroot)) == 0) {
		filename += strlen(cfg_parser->chroot);
	}
	nm = strdup(filename);
	if(!nm) {
		ub_c_error_msg("include %s: strdup failure", filename);
		free(s);
		return;
	}
	input = fopen(filename, "r");
	if(!input) {
		ub_c_error_msg("cannot open include file '%s': %s",
			filename, strerror(errno));
		free(s);
		free(nm);
		return;
	}
	LEXOUT(("switch_to_include_file(%s)\n", filename));
	s->filename = cfg_parser->filename;
	s->line = cfg_parser->line;
	s->buffer = YY_CURRENT_BUFFER;
	s->next = config_include_stack;
	config_include_stack = s;
	cfg_parser->filename = nm;
	cfg_parser->line = 1;
	yy_switch_to_buffer(yy_create_buffer(input, YY_BUF_SIZE));
}

static void config_start_include_glob(const char* filename)
{

	/* check for wildcards */
#ifdef HAVE_GLOB
	glob_t g;
	size_t i;
	int r, flags;
	if(!(!strchr(filename, '*') && !strchr(filename, '?') && !strchr(filename, '[') &&
		!strchr(filename, '{') && !strchr(filename, '~'))) {
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
		if(cfg_parser->chroot && strncmp(filename, cfg_parser->chroot,
			strlen(cfg_parser->chroot)) == 0) {
			filename += strlen(cfg_parser->chroot);
		}
		r = glob(filename, flags, NULL, &g);
		if(r) {
			/* some error */
			globfree(&g);
			if(r == GLOB_NOMATCH)
				return; /* no matches for pattern */
			config_start_include(filename); /* let original deal with it */
			return;
		}
		/* process files found, if any */
		for(i=0; i<(size_t)g.gl_pathc; i++) {
			config_start_include(g.gl_pathv[i]);
		}
		globfree(&g);
		return;
	}
#endif /* HAVE_GLOB */

	config_start_include(filename);
}

static void config_end_include(void)
{
	struct inc_state* s = config_include_stack;
	--inc_depth;
	if(!s) return;
	free(cfg_parser->filename);
	cfg_parser->filename = s->filename;
	cfg_parser->line = s->line;
	yy_delete_buffer(YY_CURRENT_BUFFER);
	yy_switch_to_buffer(s->buffer);
	config_include_stack = s->next;
	free(s);
}

#ifndef yy_set_bol /* compat definition, for flex 2.4.6 */
#define yy_set_bol(at_bol) \
        { \
	        if ( ! yy_current_buffer ) \
	                yy_current_buffer = yy_create_buffer( ub_c_in, YY_BUF_SIZE ); \
	        yy_current_buffer->yy_ch_buf[0] = ((at_bol)?'\n':' '); \
        }
#endif

%}
%option noinput
%option nounput
%{
#ifndef YY_NO_UNPUT
#define YY_NO_UNPUT 1
#endif
#ifndef YY_NO_INPUT
#define YY_NO_INPUT 1
#endif
%}

SPACE   [ \t]
LETTER  [a-zA-Z]
UNQUOTEDLETTER [^\'\"\n\r \t\\]|\\.
UNQUOTEDLETTER_NOCOLON [^\:\'\"\n\r \t\\]|\\.
NEWLINE [\r\n]
COMMENT \#
COLON 	\:
DQANY     [^\"\n\r\\]|\\.
SQANY     [^\'\n\r\\]|\\.

%x	quotedstring singlequotedstr include include_quoted val

%%
<INITIAL,val>{SPACE}*	{ 
	LEXOUT(("SP ")); /* ignore */ }
<INITIAL,val>{SPACE}*{COMMENT}.*	{ 
	/* note that flex makes the longest match and '.' is any but not nl */
	LEXOUT(("comment(%s) ", ub_c_text)); /* ignore */ }
server{COLON}			{ YDVAR(0, VAR_SERVER) }
qname-minimisation{COLON}	{ YDVAR(1, VAR_QNAME_MINIMISATION) }
qname-minimisation-strict{COLON} { YDVAR(1, VAR_QNAME_MINIMISATION_STRICT) }
num-threads{COLON}		{ YDVAR(1, VAR_NUM_THREADS) }
verbosity{COLON}		{ YDVAR(1, VAR_VERBOSITY) }
port{COLON}			{ YDVAR(1, VAR_PORT) }
outgoing-range{COLON}		{ YDVAR(1, VAR_OUTGOING_RANGE) }
outgoing-port-permit{COLON}	{ YDVAR(1, VAR_OUTGOING_PORT_PERMIT) }
outgoing-port-avoid{COLON}	{ YDVAR(1, VAR_OUTGOING_PORT_AVOID) }
outgoing-num-tcp{COLON}		{ YDVAR(1, VAR_OUTGOING_NUM_TCP) }
incoming-num-tcp{COLON}		{ YDVAR(1, VAR_INCOMING_NUM_TCP) }
do-ip4{COLON}			{ YDVAR(1, VAR_DO_IP4) }
do-ip6{COLON}			{ YDVAR(1, VAR_DO_IP6) }
prefer-ip6{COLON}		{ YDVAR(1, VAR_PREFER_IP6) }
do-udp{COLON}			{ YDVAR(1, VAR_DO_UDP) }
do-tcp{COLON}			{ YDVAR(1, VAR_DO_TCP) }
tcp-upstream{COLON}		{ YDVAR(1, VAR_TCP_UPSTREAM) }
tcp-mss{COLON}			{ YDVAR(1, VAR_TCP_MSS) }
outgoing-tcp-mss{COLON}		{ YDVAR(1, VAR_OUTGOING_TCP_MSS) }
tcp-idle-timeout{COLON}		{ YDVAR(1, VAR_TCP_IDLE_TIMEOUT) }
edns-tcp-keepalive{COLON}	{ YDVAR(1, VAR_EDNS_TCP_KEEPALIVE) }
edns-tcp-keepalive-timeout{COLON} { YDVAR(1, VAR_EDNS_TCP_KEEPALIVE_TIMEOUT) }
ssl-upstream{COLON}		{ YDVAR(1, VAR_SSL_UPSTREAM) }
tls-upstream{COLON}		{ YDVAR(1, VAR_SSL_UPSTREAM) }
ssl-service-key{COLON}		{ YDVAR(1, VAR_SSL_SERVICE_KEY) }
tls-service-key{COLON}		{ YDVAR(1, VAR_SSL_SERVICE_KEY) }
ssl-service-pem{COLON}		{ YDVAR(1, VAR_SSL_SERVICE_PEM) }
tls-service-pem{COLON}		{ YDVAR(1, VAR_SSL_SERVICE_PEM) }
ssl-port{COLON}			{ YDVAR(1, VAR_SSL_PORT) }
tls-port{COLON}			{ YDVAR(1, VAR_SSL_PORT) }
ssl-cert-bundle{COLON}		{ YDVAR(1, VAR_TLS_CERT_BUNDLE) }
tls-cert-bundle{COLON}		{ YDVAR(1, VAR_TLS_CERT_BUNDLE) }
tls-win-cert{COLON}		{ YDVAR(1, VAR_TLS_WIN_CERT) }
additional-ssl-port{COLON}	{ YDVAR(1, VAR_TLS_ADDITIONAL_PORT) }
additional-tls-port{COLON}	{ YDVAR(1, VAR_TLS_ADDITIONAL_PORT) }
tls-additional-ports{COLON}	{ YDVAR(1, VAR_TLS_ADDITIONAL_PORT) }
tls-additional-port{COLON}	{ YDVAR(1, VAR_TLS_ADDITIONAL_PORT) }
use-systemd{COLON}		{ YDVAR(1, VAR_USE_SYSTEMD) }
do-daemonize{COLON}		{ YDVAR(1, VAR_DO_DAEMONIZE) }
interface{COLON}		{ YDVAR(1, VAR_INTERFACE) }
ip-address{COLON}		{ YDVAR(1, VAR_INTERFACE) }
outgoing-interface{COLON}	{ YDVAR(1, VAR_OUTGOING_INTERFACE) }
interface-automatic{COLON}	{ YDVAR(1, VAR_INTERFACE_AUTOMATIC) }
so-rcvbuf{COLON}		{ YDVAR(1, VAR_SO_RCVBUF) }
so-sndbuf{COLON}		{ YDVAR(1, VAR_SO_SNDBUF) }
so-reuseport{COLON}		{ YDVAR(1, VAR_SO_REUSEPORT) }
ip-transparent{COLON}		{ YDVAR(1, VAR_IP_TRANSPARENT) }
ip-freebind{COLON}		{ YDVAR(1, VAR_IP_FREEBIND) }
chroot{COLON}			{ YDVAR(1, VAR_CHROOT) }
username{COLON}			{ YDVAR(1, VAR_USERNAME) }
directory{COLON}		{ YDVAR(1, VAR_DIRECTORY) }
logfile{COLON}			{ YDVAR(1, VAR_LOGFILE) }
pidfile{COLON}			{ YDVAR(1, VAR_PIDFILE) }
root-hints{COLON}		{ YDVAR(1, VAR_ROOT_HINTS) }
edns-buffer-size{COLON}		{ YDVAR(1, VAR_EDNS_BUFFER_SIZE) }
msg-buffer-size{COLON}		{ YDVAR(1, VAR_MSG_BUFFER_SIZE) }
msg-cache-size{COLON}		{ YDVAR(1, VAR_MSG_CACHE_SIZE) }
msg-cache-slabs{COLON}		{ YDVAR(1, VAR_MSG_CACHE_SLABS) }
rrset-cache-size{COLON}		{ YDVAR(1, VAR_RRSET_CACHE_SIZE) }
rrset-cache-slabs{COLON}	{ YDVAR(1, VAR_RRSET_CACHE_SLABS) }
cache-max-ttl{COLON}     	{ YDVAR(1, VAR_CACHE_MAX_TTL) }
cache-max-negative-ttl{COLON}   { YDVAR(1, VAR_CACHE_MAX_NEGATIVE_TTL) }
cache-min-ttl{COLON}     	{ YDVAR(1, VAR_CACHE_MIN_TTL) }
infra-host-ttl{COLON}		{ YDVAR(1, VAR_INFRA_HOST_TTL) }
infra-lame-ttl{COLON}		{ YDVAR(1, VAR_INFRA_LAME_TTL) }
infra-cache-slabs{COLON}	{ YDVAR(1, VAR_INFRA_CACHE_SLABS) }
infra-cache-numhosts{COLON}	{ YDVAR(1, VAR_INFRA_CACHE_NUMHOSTS) }
infra-cache-lame-size{COLON}	{ YDVAR(1, VAR_INFRA_CACHE_LAME_SIZE) }
infra-cache-min-rtt{COLON}	{ YDVAR(1, VAR_INFRA_CACHE_MIN_RTT) }
num-queries-per-thread{COLON}	{ YDVAR(1, VAR_NUM_QUERIES_PER_THREAD) }
jostle-timeout{COLON}		{ YDVAR(1, VAR_JOSTLE_TIMEOUT) }
delay-close{COLON}		{ YDVAR(1, VAR_DELAY_CLOSE) }
target-fetch-policy{COLON}	{ YDVAR(1, VAR_TARGET_FETCH_POLICY) }
harden-short-bufsize{COLON}	{ YDVAR(1, VAR_HARDEN_SHORT_BUFSIZE) }
harden-large-queries{COLON}	{ YDVAR(1, VAR_HARDEN_LARGE_QUERIES) }
harden-glue{COLON}		{ YDVAR(1, VAR_HARDEN_GLUE) }
harden-dnssec-stripped{COLON}	{ YDVAR(1, VAR_HARDEN_DNSSEC_STRIPPED) }
harden-below-nxdomain{COLON}	{ YDVAR(1, VAR_HARDEN_BELOW_NXDOMAIN) }
harden-referral-path{COLON}	{ YDVAR(1, VAR_HARDEN_REFERRAL_PATH) }
harden-algo-downgrade{COLON}	{ YDVAR(1, VAR_HARDEN_ALGO_DOWNGRADE) }
use-caps-for-id{COLON}		{ YDVAR(1, VAR_USE_CAPS_FOR_ID) }
caps-whitelist{COLON}		{ YDVAR(1, VAR_CAPS_WHITELIST) }
unwanted-reply-threshold{COLON}	{ YDVAR(1, VAR_UNWANTED_REPLY_THRESHOLD) }
private-address{COLON}		{ YDVAR(1, VAR_PRIVATE_ADDRESS) }
private-domain{COLON}		{ YDVAR(1, VAR_PRIVATE_DOMAIN) }
prefetch-key{COLON}		{ YDVAR(1, VAR_PREFETCH_KEY) }
prefetch{COLON}			{ YDVAR(1, VAR_PREFETCH) }
stub-zone{COLON}		{ YDVAR(0, VAR_STUB_ZONE) }
name{COLON}			{ YDVAR(1, VAR_NAME) }
stub-addr{COLON}		{ YDVAR(1, VAR_STUB_ADDR) }
stub-host{COLON}		{ YDVAR(1, VAR_STUB_HOST) }
stub-prime{COLON}		{ YDVAR(1, VAR_STUB_PRIME) }
stub-first{COLON}		{ YDVAR(1, VAR_STUB_FIRST) }
stub-no-cache{COLON}		{ YDVAR(1, VAR_STUB_NO_CACHE) }
stub-ssl-upstream{COLON}	{ YDVAR(1, VAR_STUB_SSL_UPSTREAM) }
stub-tls-upstream{COLON}	{ YDVAR(1, VAR_STUB_SSL_UPSTREAM) }
forward-zone{COLON}		{ YDVAR(0, VAR_FORWARD_ZONE) }
forward-addr{COLON}		{ YDVAR(1, VAR_FORWARD_ADDR) }
forward-host{COLON}		{ YDVAR(1, VAR_FORWARD_HOST) }
forward-first{COLON}		{ YDVAR(1, VAR_FORWARD_FIRST) }
forward-no-cache{COLON}		{ YDVAR(1, VAR_FORWARD_NO_CACHE) }
forward-ssl-upstream{COLON}	{ YDVAR(1, VAR_FORWARD_SSL_UPSTREAM) }
forward-tls-upstream{COLON}	{ YDVAR(1, VAR_FORWARD_SSL_UPSTREAM) }
auth-zone{COLON}		{ YDVAR(0, VAR_AUTH_ZONE) }
zonefile{COLON}			{ YDVAR(1, VAR_ZONEFILE) }
master{COLON}			{ YDVAR(1, VAR_MASTER) }
url{COLON}			{ YDVAR(1, VAR_URL) }
allow-notify{COLON}		{ YDVAR(1, VAR_ALLOW_NOTIFY) }
for-downstream{COLON}		{ YDVAR(1, VAR_FOR_DOWNSTREAM) }
for-upstream{COLON}		{ YDVAR(1, VAR_FOR_UPSTREAM) }
fallback-enabled{COLON}		{ YDVAR(1, VAR_FALLBACK_ENABLED) }
view{COLON}			{ YDVAR(0, VAR_VIEW) }
view-first{COLON}		{ YDVAR(1, VAR_VIEW_FIRST) }
do-not-query-address{COLON}	{ YDVAR(1, VAR_DO_NOT_QUERY_ADDRESS) }
do-not-query-localhost{COLON}	{ YDVAR(1, VAR_DO_NOT_QUERY_LOCALHOST) }
access-control{COLON}		{ YDVAR(2, VAR_ACCESS_CONTROL) }
send-client-subnet{COLON}	{ YDVAR(1, VAR_SEND_CLIENT_SUBNET) }
client-subnet-zone{COLON}	{ YDVAR(1, VAR_CLIENT_SUBNET_ZONE) }
client-subnet-always-forward{COLON} { YDVAR(1, VAR_CLIENT_SUBNET_ALWAYS_FORWARD) }
client-subnet-opcode{COLON}	{ YDVAR(1, VAR_CLIENT_SUBNET_OPCODE) }
max-client-subnet-ipv4{COLON}	{ YDVAR(1, VAR_MAX_CLIENT_SUBNET_IPV4) }
max-client-subnet-ipv6{COLON}	{ YDVAR(1, VAR_MAX_CLIENT_SUBNET_IPV6) }
hide-identity{COLON}		{ YDVAR(1, VAR_HIDE_IDENTITY) }
hide-version{COLON}		{ YDVAR(1, VAR_HIDE_VERSION) }
hide-trustanchor{COLON}		{ YDVAR(1, VAR_HIDE_TRUSTANCHOR) }
identity{COLON}			{ YDVAR(1, VAR_IDENTITY) }
version{COLON}			{ YDVAR(1, VAR_VERSION) }
module-config{COLON}     	{ YDVAR(1, VAR_MODULE_CONF) }
dlv-anchor{COLON}		{ YDVAR(1, VAR_DLV_ANCHOR) }
dlv-anchor-file{COLON}		{ YDVAR(1, VAR_DLV_ANCHOR_FILE) }
trust-anchor-file{COLON}	{ YDVAR(1, VAR_TRUST_ANCHOR_FILE) }
auto-trust-anchor-file{COLON}	{ YDVAR(1, VAR_AUTO_TRUST_ANCHOR_FILE) }
trusted-keys-file{COLON}	{ YDVAR(1, VAR_TRUSTED_KEYS_FILE) }
trust-anchor{COLON}		{ YDVAR(1, VAR_TRUST_ANCHOR) }
trust-anchor-signaling{COLON}	{ YDVAR(1, VAR_TRUST_ANCHOR_SIGNALING) }
root-key-sentinel{COLON}	{ YDVAR(1, VAR_ROOT_KEY_SENTINEL) }
val-override-date{COLON}	{ YDVAR(1, VAR_VAL_OVERRIDE_DATE) }
val-sig-skew-min{COLON}		{ YDVAR(1, VAR_VAL_SIG_SKEW_MIN) }
val-sig-skew-max{COLON}		{ YDVAR(1, VAR_VAL_SIG_SKEW_MAX) }
val-bogus-ttl{COLON}		{ YDVAR(1, VAR_BOGUS_TTL) }
val-clean-additional{COLON}	{ YDVAR(1, VAR_VAL_CLEAN_ADDITIONAL) }
val-permissive-mode{COLON}	{ YDVAR(1, VAR_VAL_PERMISSIVE_MODE) }
aggressive-nsec{COLON}		{ YDVAR(1, VAR_AGGRESSIVE_NSEC) }
ignore-cd-flag{COLON}		{ YDVAR(1, VAR_IGNORE_CD_FLAG) }
serve-expired{COLON}		{ YDVAR(1, VAR_SERVE_EXPIRED) }
serve-expired-ttl{COLON}	{ YDVAR(1, VAR_SERVE_EXPIRED_TTL) }
serve-expired-ttl-reset{COLON}	{ YDVAR(1, VAR_SERVE_EXPIRED_TTL_RESET) }
fake-dsa{COLON}			{ YDVAR(1, VAR_FAKE_DSA) }
fake-sha1{COLON}		{ YDVAR(1, VAR_FAKE_SHA1) }
val-log-level{COLON}		{ YDVAR(1, VAR_VAL_LOG_LEVEL) }
key-cache-size{COLON}		{ YDVAR(1, VAR_KEY_CACHE_SIZE) }
key-cache-slabs{COLON}		{ YDVAR(1, VAR_KEY_CACHE_SLABS) }
neg-cache-size{COLON}		{ YDVAR(1, VAR_NEG_CACHE_SIZE) }
val-nsec3-keysize-iterations{COLON}	{ 
				  YDVAR(1, VAR_VAL_NSEC3_KEYSIZE_ITERATIONS) }
add-holddown{COLON}		{ YDVAR(1, VAR_ADD_HOLDDOWN) }
del-holddown{COLON}		{ YDVAR(1, VAR_DEL_HOLDDOWN) }
keep-missing{COLON}		{ YDVAR(1, VAR_KEEP_MISSING) }
permit-small-holddown{COLON}	{ YDVAR(1, VAR_PERMIT_SMALL_HOLDDOWN) }
use-syslog{COLON}		{ YDVAR(1, VAR_USE_SYSLOG) }
log-identity{COLON}		{ YDVAR(1, VAR_LOG_IDENTITY) }
log-time-ascii{COLON}		{ YDVAR(1, VAR_LOG_TIME_ASCII) }
log-queries{COLON}		{ YDVAR(1, VAR_LOG_QUERIES) }
log-replies{COLON}		{ YDVAR(1, VAR_LOG_REPLIES) }
log-local-actions{COLON}       { YDVAR(1, VAR_LOG_LOCAL_ACTIONS) }
log-servfail{COLON}		{ YDVAR(1, VAR_LOG_SERVFAIL) }
local-zone{COLON}		{ YDVAR(2, VAR_LOCAL_ZONE) }
local-data{COLON}		{ YDVAR(1, VAR_LOCAL_DATA) }
local-data-ptr{COLON}		{ YDVAR(1, VAR_LOCAL_DATA_PTR) }
unblock-lan-zones{COLON}	{ YDVAR(1, VAR_UNBLOCK_LAN_ZONES) }
insecure-lan-zones{COLON}	{ YDVAR(1, VAR_INSECURE_LAN_ZONES) }
statistics-interval{COLON}	{ YDVAR(1, VAR_STATISTICS_INTERVAL) }
statistics-cumulative{COLON}	{ YDVAR(1, VAR_STATISTICS_CUMULATIVE) }
extended-statistics{COLON}	{ YDVAR(1, VAR_EXTENDED_STATISTICS) }
shm-enable{COLON}		{ YDVAR(1, VAR_SHM_ENABLE) }
shm-key{COLON}			{ YDVAR(1, VAR_SHM_KEY) }
remote-control{COLON}		{ YDVAR(0, VAR_REMOTE_CONTROL) }
control-enable{COLON}		{ YDVAR(1, VAR_CONTROL_ENABLE) }
control-interface{COLON}	{ YDVAR(1, VAR_CONTROL_INTERFACE) }
control-port{COLON}		{ YDVAR(1, VAR_CONTROL_PORT) }
control-use-cert{COLON}		{ YDVAR(1, VAR_CONTROL_USE_CERT) }
server-key-file{COLON}		{ YDVAR(1, VAR_SERVER_KEY_FILE) }
server-cert-file{COLON}		{ YDVAR(1, VAR_SERVER_CERT_FILE) }
control-key-file{COLON}		{ YDVAR(1, VAR_CONTROL_KEY_FILE) }
control-cert-file{COLON}	{ YDVAR(1, VAR_CONTROL_CERT_FILE) }
python-script{COLON}		{ YDVAR(1, VAR_PYTHON_SCRIPT) }
python{COLON}			{ YDVAR(0, VAR_PYTHON) }
domain-insecure{COLON}		{ YDVAR(1, VAR_DOMAIN_INSECURE) }
minimal-responses{COLON}	{ YDVAR(1, VAR_MINIMAL_RESPONSES) }
rrset-roundrobin{COLON}		{ YDVAR(1, VAR_RRSET_ROUNDROBIN) }
max-udp-size{COLON}		{ YDVAR(1, VAR_MAX_UDP_SIZE) }
dns64-prefix{COLON}		{ YDVAR(1, VAR_DNS64_PREFIX) }
dns64-synthall{COLON}		{ YDVAR(1, VAR_DNS64_SYNTHALL) }
dns64-ignore-aaaa{COLON}	{ YDVAR(1, VAR_DNS64_IGNORE_AAAA) }
define-tag{COLON}		{ YDVAR(1, VAR_DEFINE_TAG) }
local-zone-tag{COLON}		{ YDVAR(2, VAR_LOCAL_ZONE_TAG) }
access-control-tag{COLON}	{ YDVAR(2, VAR_ACCESS_CONTROL_TAG) }
access-control-tag-action{COLON} { YDVAR(3, VAR_ACCESS_CONTROL_TAG_ACTION) }
access-control-tag-data{COLON}	{ YDVAR(3, VAR_ACCESS_CONTROL_TAG_DATA) }
access-control-view{COLON}	{ YDVAR(2, VAR_ACCESS_CONTROL_VIEW) }
local-zone-override{COLON}	{ YDVAR(3, VAR_LOCAL_ZONE_OVERRIDE) }
dnstap{COLON}			{ YDVAR(0, VAR_DNSTAP) }
dnstap-enable{COLON}		{ YDVAR(1, VAR_DNSTAP_ENABLE) }
dnstap-socket-path{COLON}	{ YDVAR(1, VAR_DNSTAP_SOCKET_PATH) }
dnstap-send-identity{COLON}	{ YDVAR(1, VAR_DNSTAP_SEND_IDENTITY) }
dnstap-send-version{COLON}	{ YDVAR(1, VAR_DNSTAP_SEND_VERSION) }
dnstap-identity{COLON}		{ YDVAR(1, VAR_DNSTAP_IDENTITY) }
dnstap-version{COLON}		{ YDVAR(1, VAR_DNSTAP_VERSION) }
dnstap-log-resolver-query-messages{COLON}	{
		YDVAR(1, VAR_DNSTAP_LOG_RESOLVER_QUERY_MESSAGES) }
dnstap-log-resolver-response-messages{COLON}	{
		YDVAR(1, VAR_DNSTAP_LOG_RESOLVER_RESPONSE_MESSAGES) }
dnstap-log-client-query-messages{COLON}		{
		YDVAR(1, VAR_DNSTAP_LOG_CLIENT_QUERY_MESSAGES) }
dnstap-log-client-response-messages{COLON}	{
		YDVAR(1, VAR_DNSTAP_LOG_CLIENT_RESPONSE_MESSAGES) }
dnstap-log-forwarder-query-messages{COLON}	{
		YDVAR(1, VAR_DNSTAP_LOG_FORWARDER_QUERY_MESSAGES) }
dnstap-log-forwarder-response-messages{COLON}	{
		YDVAR(1, VAR_DNSTAP_LOG_FORWARDER_RESPONSE_MESSAGES) }
disable-dnssec-lame-check{COLON} { YDVAR(1, VAR_DISABLE_DNSSEC_LAME_CHECK) }
ip-ratelimit{COLON}		{ YDVAR(1, VAR_IP_RATELIMIT) }
ratelimit{COLON}		{ YDVAR(1, VAR_RATELIMIT) }
ip-ratelimit-slabs{COLON}		{ YDVAR(1, VAR_IP_RATELIMIT_SLABS) }
ratelimit-slabs{COLON}		{ YDVAR(1, VAR_RATELIMIT_SLABS) }
ip-ratelimit-size{COLON}		{ YDVAR(1, VAR_IP_RATELIMIT_SIZE) }
ratelimit-size{COLON}		{ YDVAR(1, VAR_RATELIMIT_SIZE) }
ratelimit-for-domain{COLON}	{ YDVAR(2, VAR_RATELIMIT_FOR_DOMAIN) }
ratelimit-below-domain{COLON}	{ YDVAR(2, VAR_RATELIMIT_BELOW_DOMAIN) }
ip-ratelimit-factor{COLON}		{ YDVAR(1, VAR_IP_RATELIMIT_FACTOR) }
ratelimit-factor{COLON}		{ YDVAR(1, VAR_RATELIMIT_FACTOR) }
low-rtt{COLON}			{ YDVAR(1, VAR_LOW_RTT) }
low-rtt-pct{COLON}		{ YDVAR(1, VAR_LOW_RTT_PERMIL) }
low-rtt-permil{COLON}		{ YDVAR(1, VAR_LOW_RTT_PERMIL) }
response-ip-tag{COLON}		{ YDVAR(2, VAR_RESPONSE_IP_TAG) }
response-ip{COLON}		{ YDVAR(2, VAR_RESPONSE_IP) }
response-ip-data{COLON}		{ YDVAR(2, VAR_RESPONSE_IP_DATA) }
dnscrypt{COLON}			{ YDVAR(0, VAR_DNSCRYPT) }
dnscrypt-enable{COLON}		{ YDVAR(1, VAR_DNSCRYPT_ENABLE) }
dnscrypt-port{COLON}		{ YDVAR(1, VAR_DNSCRYPT_PORT) }
dnscrypt-provider{COLON}	{ YDVAR(1, VAR_DNSCRYPT_PROVIDER) }
dnscrypt-secret-key{COLON}	{ YDVAR(1, VAR_DNSCRYPT_SECRET_KEY) }
dnscrypt-provider-cert{COLON}	{ YDVAR(1, VAR_DNSCRYPT_PROVIDER_CERT) }
dnscrypt-provider-cert-rotated{COLON}	{ YDVAR(1, VAR_DNSCRYPT_PROVIDER_CERT_ROTATED) }
dnscrypt-shared-secret-cache-size{COLON}	{
		YDVAR(1, VAR_DNSCRYPT_SHARED_SECRET_CACHE_SIZE) }
dnscrypt-shared-secret-cache-slabs{COLON}	{
		YDVAR(1, VAR_DNSCRYPT_SHARED_SECRET_CACHE_SLABS) }
dnscrypt-nonce-cache-size{COLON}	{ YDVAR(1, VAR_DNSCRYPT_NONCE_CACHE_SIZE) }
dnscrypt-nonce-cache-slabs{COLON}	{ YDVAR(1, VAR_DNSCRYPT_NONCE_CACHE_SLABS) }
ipsecmod-enabled{COLON}		{ YDVAR(1, VAR_IPSECMOD_ENABLED) }
ipsecmod-ignore-bogus{COLON}	{ YDVAR(1, VAR_IPSECMOD_IGNORE_BOGUS) }
ipsecmod-hook{COLON}		{ YDVAR(1, VAR_IPSECMOD_HOOK) }
ipsecmod-max-ttl{COLON}		{ YDVAR(1, VAR_IPSECMOD_MAX_TTL) }
ipsecmod-whitelist{COLON}	{ YDVAR(1, VAR_IPSECMOD_WHITELIST) }
ipsecmod-strict{COLON}		{ YDVAR(1, VAR_IPSECMOD_STRICT) }
cachedb{COLON}			{ YDVAR(0, VAR_CACHEDB) }
backend{COLON}			{ YDVAR(1, VAR_CACHEDB_BACKEND) }
secret-seed{COLON}		{ YDVAR(1, VAR_CACHEDB_SECRETSEED) }
redis-server-host{COLON}	{ YDVAR(1, VAR_CACHEDB_REDISHOST) }
redis-server-port{COLON}	{ YDVAR(1, VAR_CACHEDB_REDISPORT) }
redis-timeout{COLON}		{ YDVAR(1, VAR_CACHEDB_REDISTIMEOUT) }
udp-upstream-without-downstream{COLON} { YDVAR(1, VAR_UDP_UPSTREAM_WITHOUT_DOWNSTREAM) }
tcp-connection-limit{COLON}	{ YDVAR(2, VAR_TCP_CONNECTION_LIMIT) }
<INITIAL,val>{NEWLINE}		{ LEXOUT(("NL\n")); cfg_parser->line++; }

	/* Quoted strings. Strip leading and ending quotes */
<val>\"			{ BEGIN(quotedstring); LEXOUT(("QS ")); }
<quotedstring><<EOF>>   {
        ub_c_error("EOF inside quoted string");
	if(--num_args == 0) { BEGIN(INITIAL); }
	else		    { BEGIN(val); }
}
<quotedstring>{DQANY}*  { LEXOUT(("STR(%s) ", ub_c_text)); yymore(); }
<quotedstring>{NEWLINE} { ub_c_error("newline inside quoted string, no end \""); 
			  cfg_parser->line++; BEGIN(INITIAL); }
<quotedstring>\" {
        LEXOUT(("QE "));
	if(--num_args == 0) { BEGIN(INITIAL); }
	else		    { BEGIN(val); }
        ub_c_text[ub_c_leng - 1] = '\0';
	ub_c_lval.str = strdup(ub_c_text);
	if(!ub_c_lval.str)
		ub_c_error("out of memory");
        return STRING_ARG;
}

	/* Single Quoted strings. Strip leading and ending quotes */
<val>\'			{ BEGIN(singlequotedstr); LEXOUT(("SQS ")); }
<singlequotedstr><<EOF>>   {
        ub_c_error("EOF inside quoted string");
	if(--num_args == 0) { BEGIN(INITIAL); }
	else		    { BEGIN(val); }
}
<singlequotedstr>{SQANY}*  { LEXOUT(("STR(%s) ", ub_c_text)); yymore(); }
<singlequotedstr>{NEWLINE} { ub_c_error("newline inside quoted string, no end '"); 
			     cfg_parser->line++; BEGIN(INITIAL); }
<singlequotedstr>\' {
        LEXOUT(("SQE "));
	if(--num_args == 0) { BEGIN(INITIAL); }
	else		    { BEGIN(val); }
        ub_c_text[ub_c_leng - 1] = '\0';
	ub_c_lval.str = strdup(ub_c_text);
	if(!ub_c_lval.str)
		ub_c_error("out of memory");
        return STRING_ARG;
}

	/* include: directive */
<INITIAL,val>include{COLON}	{ 
	LEXOUT(("v(%s) ", ub_c_text)); inc_prev = YYSTATE; BEGIN(include); }
<include><<EOF>>	{
        ub_c_error("EOF inside include directive");
        BEGIN(inc_prev);
}
<include>{SPACE}*	{ LEXOUT(("ISP ")); /* ignore */ }
<include>{NEWLINE}	{ LEXOUT(("NL\n")); cfg_parser->line++;}
<include>\"		{ LEXOUT(("IQS ")); BEGIN(include_quoted); }
<include>{UNQUOTEDLETTER}*	{
	LEXOUT(("Iunquotedstr(%s) ", ub_c_text));
	config_start_include_glob(ub_c_text);
	BEGIN(inc_prev);
}
<include_quoted><<EOF>>	{
        ub_c_error("EOF inside quoted string");
        BEGIN(inc_prev);
}
<include_quoted>{DQANY}*	{ LEXOUT(("ISTR(%s) ", ub_c_text)); yymore(); }
<include_quoted>{NEWLINE}	{ ub_c_error("newline before \" in include name"); 
				  cfg_parser->line++; BEGIN(inc_prev); }
<include_quoted>\"	{
	LEXOUT(("IQE "));
	ub_c_text[ub_c_leng - 1] = '\0';
	config_start_include_glob(ub_c_text);
	BEGIN(inc_prev);
}
<INITIAL,val><<EOF>>	{
	LEXOUT(("LEXEOF "));
	yy_set_bol(1); /* Set beginning of line, so "^" rules match.  */
	if (!config_include_stack) {
		yyterminate();
	} else {
		fclose(ub_c_in);
		config_end_include();
	}
}

<val>{UNQUOTEDLETTER}*	{ LEXOUT(("unquotedstr(%s) ", ub_c_text)); 
			if(--num_args == 0) { BEGIN(INITIAL); }
			ub_c_lval.str = strdup(ub_c_text); return STRING_ARG; }

{UNQUOTEDLETTER_NOCOLON}*	{
	ub_c_error_msg("unknown keyword '%s'", ub_c_text);
	}

<*>.	{
	ub_c_error_msg("stray '%s'", ub_c_text);
	}

%%
