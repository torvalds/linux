
/* $OpenBSD: servconf.c,v 1.340 2018/08/12 20:19:13 djm Exp $ */
/*
 * Copyright (c) 1995 Tatu Ylonen <ylo@cs.hut.fi>, Espoo, Finland
 *                    All rights reserved
 *
 * As far as I am concerned, the code I have written for this software
 * can be used freely for any purpose.  Any derived versions of this
 * software must be clearly marked as such, and if the derived work is
 * incompatible with the protocol description in the RFC file, it must be
 * called by a name other than "ssh" or "Secure Shell".
 */

#include "includes.h"
__RCSID("$FreeBSD$");

#include <sys/types.h>
#include <sys/socket.h>
#ifdef HAVE_SYS_SYSCTL_H
#include <sys/sysctl.h>
#endif

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#ifdef HAVE_NET_ROUTE_H
#include <net/route.h>
#endif

#include <ctype.h>
#include <netdb.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <limits.h>
#include <stdarg.h>
#include <errno.h>
#ifdef HAVE_UTIL_H
#include <util.h>
#endif

#include "openbsd-compat/sys-queue.h"
#include "xmalloc.h"
#include "ssh.h"
#include "log.h"
#include "sshbuf.h"
#include "misc.h"
#include "servconf.h"
#include "compat.h"
#include "pathnames.h"
#include "cipher.h"
#include "sshkey.h"
#include "kex.h"
#include "mac.h"
#include "match.h"
#include "channels.h"
#include "groupaccess.h"
#include "canohost.h"
#include "packet.h"
#include "ssherr.h"
#include "hostfile.h"
#include "auth.h"
#include "myproposal.h"
#include "digest.h"
#include "version.h"

static void add_listen_addr(ServerOptions *, const char *,
    const char *, int);
static void add_one_listen_addr(ServerOptions *, const char *,
    const char *, int);

/* Use of privilege separation or not */
extern int use_privsep;
extern struct sshbuf *cfg;

/* Initializes the server options to their default values. */

void
initialize_server_options(ServerOptions *options)
{
	memset(options, 0, sizeof(*options));

	/* Portable-specific options */
	options->use_pam = -1;

	/* Standard Options */
	options->num_ports = 0;
	options->ports_from_cmdline = 0;
	options->queued_listen_addrs = NULL;
	options->num_queued_listens = 0;
	options->listen_addrs = NULL;
	options->num_listen_addrs = 0;
	options->address_family = -1;
	options->routing_domain = NULL;
	options->num_host_key_files = 0;
	options->num_host_cert_files = 0;
	options->host_key_agent = NULL;
	options->pid_file = NULL;
	options->login_grace_time = -1;
	options->permit_root_login = PERMIT_NOT_SET;
	options->ignore_rhosts = -1;
	options->ignore_user_known_hosts = -1;
	options->print_motd = -1;
	options->print_lastlog = -1;
	options->x11_forwarding = -1;
	options->x11_display_offset = -1;
	options->x11_use_localhost = -1;
	options->permit_tty = -1;
	options->permit_user_rc = -1;
	options->xauth_location = NULL;
	options->strict_modes = -1;
	options->tcp_keep_alive = -1;
	options->log_facility = SYSLOG_FACILITY_NOT_SET;
	options->log_level = SYSLOG_LEVEL_NOT_SET;
	options->hostbased_authentication = -1;
	options->hostbased_uses_name_from_packet_only = -1;
	options->hostbased_key_types = NULL;
	options->hostkeyalgorithms = NULL;
	options->pubkey_authentication = -1;
	options->pubkey_key_types = NULL;
	options->kerberos_authentication = -1;
	options->kerberos_or_local_passwd = -1;
	options->kerberos_ticket_cleanup = -1;
	options->kerberos_get_afs_token = -1;
	options->gss_authentication=-1;
	options->gss_cleanup_creds = -1;
	options->gss_strict_acceptor = -1;
	options->password_authentication = -1;
	options->kbd_interactive_authentication = -1;
	options->challenge_response_authentication = -1;
	options->permit_empty_passwd = -1;
	options->permit_user_env = -1;
	options->permit_user_env_whitelist = NULL;
	options->compression = -1;
	options->rekey_limit = -1;
	options->rekey_interval = -1;
	options->allow_tcp_forwarding = -1;
	options->allow_streamlocal_forwarding = -1;
	options->allow_agent_forwarding = -1;
	options->num_allow_users = 0;
	options->num_deny_users = 0;
	options->num_allow_groups = 0;
	options->num_deny_groups = 0;
	options->ciphers = NULL;
	options->macs = NULL;
	options->kex_algorithms = NULL;
	options->fwd_opts.gateway_ports = -1;
	options->fwd_opts.streamlocal_bind_mask = (mode_t)-1;
	options->fwd_opts.streamlocal_bind_unlink = -1;
	options->num_subsystems = 0;
	options->max_startups_begin = -1;
	options->max_startups_rate = -1;
	options->max_startups = -1;
	options->max_authtries = -1;
	options->max_sessions = -1;
	options->banner = NULL;
	options->use_dns = -1;
	options->client_alive_interval = -1;
	options->client_alive_count_max = -1;
	options->num_authkeys_files = 0;
	options->num_accept_env = 0;
	options->num_setenv = 0;
	options->permit_tun = -1;
	options->permitted_opens = NULL;
	options->permitted_listens = NULL;
	options->adm_forced_command = NULL;
	options->chroot_directory = NULL;
	options->authorized_keys_command = NULL;
	options->authorized_keys_command_user = NULL;
	options->revoked_keys_file = NULL;
	options->trusted_user_ca_keys = NULL;
	options->authorized_principals_file = NULL;
	options->authorized_principals_command = NULL;
	options->authorized_principals_command_user = NULL;
	options->ip_qos_interactive = -1;
	options->ip_qos_bulk = -1;
	options->version_addendum = NULL;
	options->fingerprint_hash = -1;
	options->disable_forwarding = -1;
	options->expose_userauth_info = -1;
	options->use_blacklist = -1;
}

/* Returns 1 if a string option is unset or set to "none" or 0 otherwise. */
static int
option_clear_or_none(const char *o)
{
	return o == NULL || strcasecmp(o, "none") == 0;
}

static void
assemble_algorithms(ServerOptions *o)
{
	char *all_cipher, *all_mac, *all_kex, *all_key;
	int r;

	all_cipher = cipher_alg_list(',', 0);
	all_mac = mac_alg_list(',');
	all_kex = kex_alg_list(',');
	all_key = sshkey_alg_list(0, 0, 1, ',');
#define ASSEMBLE(what, defaults, all) \
	do { \
		if ((r = kex_assemble_names(&o->what, defaults, all)) != 0) \
			fatal("%s: %s: %s", __func__, #what, ssh_err(r)); \
	} while (0)
	ASSEMBLE(ciphers, KEX_SERVER_ENCRYPT, all_cipher);
	ASSEMBLE(macs, KEX_SERVER_MAC, all_mac);
	ASSEMBLE(kex_algorithms, KEX_SERVER_KEX, all_kex);
	ASSEMBLE(hostkeyalgorithms, KEX_DEFAULT_PK_ALG, all_key);
	ASSEMBLE(hostbased_key_types, KEX_DEFAULT_PK_ALG, all_key);
	ASSEMBLE(pubkey_key_types, KEX_DEFAULT_PK_ALG, all_key);
#undef ASSEMBLE
	free(all_cipher);
	free(all_mac);
	free(all_kex);
	free(all_key);
}

static void
array_append(const char *file, const int line, const char *directive,
    char ***array, u_int *lp, const char *s)
{

	if (*lp >= INT_MAX)
		fatal("%s line %d: Too many %s entries", file, line, directive);

	*array = xrecallocarray(*array, *lp, *lp + 1, sizeof(**array));
	(*array)[*lp] = xstrdup(s);
	(*lp)++;
}

static const char *defaultkey = "[default]";

void
servconf_add_hostkey(const char *file, const int line,
    ServerOptions *options, const char *path)
{
	char *apath = derelativise_path(path);

	if (file == defaultkey && access(path, R_OK) != 0)
		return;
	array_append(file, line, "HostKey",
	    &options->host_key_files, &options->num_host_key_files, apath);
	free(apath);
}

void
servconf_add_hostcert(const char *file, const int line,
    ServerOptions *options, const char *path)
{
	char *apath = derelativise_path(path);

	array_append(file, line, "HostCertificate",
	    &options->host_cert_files, &options->num_host_cert_files, apath);
	free(apath);
}

void
fill_default_server_options(ServerOptions *options)
{
	u_int i;

	/* Portable-specific options */
	if (options->use_pam == -1)
		options->use_pam = 1;

	/* Standard Options */
	if (options->num_host_key_files == 0) {
		/* fill default hostkeys for protocols */
		servconf_add_hostkey(defaultkey, 0, options,
		    _PATH_HOST_RSA_KEY_FILE);
		servconf_add_hostkey(defaultkey, 0, options,
		    _PATH_HOST_DSA_KEY_FILE);
#ifdef OPENSSL_HAS_ECC
		servconf_add_hostkey(defaultkey, 0, options,
		    _PATH_HOST_ECDSA_KEY_FILE);
#endif
		servconf_add_hostkey(defaultkey, 0, options,
		    _PATH_HOST_ED25519_KEY_FILE);
#ifdef WITH_XMSS
		servconf_add_hostkey(defaultkey, 0, options,
		    _PATH_HOST_XMSS_KEY_FILE);
#endif /* WITH_XMSS */
	}
	if (options->num_host_key_files == 0)
		fatal("No host key files found");
	/* No certificates by default */
	if (options->num_ports == 0)
		options->ports[options->num_ports++] = SSH_DEFAULT_PORT;
	if (options->address_family == -1)
		options->address_family = AF_UNSPEC;
	if (options->listen_addrs == NULL)
		add_listen_addr(options, NULL, NULL, 0);
	if (options->pid_file == NULL)
		options->pid_file = xstrdup(_PATH_SSH_DAEMON_PID_FILE);
	if (options->login_grace_time == -1)
		options->login_grace_time = 120;
	if (options->permit_root_login == PERMIT_NOT_SET)
		options->permit_root_login = PERMIT_NO;
	if (options->ignore_rhosts == -1)
		options->ignore_rhosts = 1;
	if (options->ignore_user_known_hosts == -1)
		options->ignore_user_known_hosts = 0;
	if (options->print_motd == -1)
		options->print_motd = 1;
	if (options->print_lastlog == -1)
		options->print_lastlog = 1;
	if (options->x11_forwarding == -1)
		options->x11_forwarding = 1;
	if (options->x11_display_offset == -1)
		options->x11_display_offset = 10;
	if (options->x11_use_localhost == -1)
		options->x11_use_localhost = 1;
	if (options->xauth_location == NULL)
		options->xauth_location = xstrdup(_PATH_XAUTH);
	if (options->permit_tty == -1)
		options->permit_tty = 1;
	if (options->permit_user_rc == -1)
		options->permit_user_rc = 1;
	if (options->strict_modes == -1)
		options->strict_modes = 1;
	if (options->tcp_keep_alive == -1)
		options->tcp_keep_alive = 1;
	if (options->log_facility == SYSLOG_FACILITY_NOT_SET)
		options->log_facility = SYSLOG_FACILITY_AUTH;
	if (options->log_level == SYSLOG_LEVEL_NOT_SET)
		options->log_level = SYSLOG_LEVEL_INFO;
	if (options->hostbased_authentication == -1)
		options->hostbased_authentication = 0;
	if (options->hostbased_uses_name_from_packet_only == -1)
		options->hostbased_uses_name_from_packet_only = 0;
	if (options->pubkey_authentication == -1)
		options->pubkey_authentication = 1;
	if (options->kerberos_authentication == -1)
		options->kerberos_authentication = 0;
	if (options->kerberos_or_local_passwd == -1)
		options->kerberos_or_local_passwd = 1;
	if (options->kerberos_ticket_cleanup == -1)
		options->kerberos_ticket_cleanup = 1;
	if (options->kerberos_get_afs_token == -1)
		options->kerberos_get_afs_token = 0;
	if (options->gss_authentication == -1)
		options->gss_authentication = 0;
	if (options->gss_cleanup_creds == -1)
		options->gss_cleanup_creds = 1;
	if (options->gss_strict_acceptor == -1)
		options->gss_strict_acceptor = 1;
	if (options->password_authentication == -1)
		options->password_authentication = 0;
	if (options->kbd_interactive_authentication == -1)
		options->kbd_interactive_authentication = 0;
	if (options->challenge_response_authentication == -1)
		options->challenge_response_authentication = 1;
	if (options->permit_empty_passwd == -1)
		options->permit_empty_passwd = 0;
	if (options->permit_user_env == -1) {
		options->permit_user_env = 0;
		options->permit_user_env_whitelist = NULL;
	}
	if (options->compression == -1)
		options->compression = COMP_DELAYED;
	if (options->rekey_limit == -1)
		options->rekey_limit = 0;
	if (options->rekey_interval == -1)
		options->rekey_interval = 0;
	if (options->allow_tcp_forwarding == -1)
		options->allow_tcp_forwarding = FORWARD_ALLOW;
	if (options->allow_streamlocal_forwarding == -1)
		options->allow_streamlocal_forwarding = FORWARD_ALLOW;
	if (options->allow_agent_forwarding == -1)
		options->allow_agent_forwarding = 1;
	if (options->fwd_opts.gateway_ports == -1)
		options->fwd_opts.gateway_ports = 0;
	if (options->max_startups == -1)
		options->max_startups = 100;
	if (options->max_startups_rate == -1)
		options->max_startups_rate = 30;		/* 30% */
	if (options->max_startups_begin == -1)
		options->max_startups_begin = 10;
	if (options->max_authtries == -1)
		options->max_authtries = DEFAULT_AUTH_FAIL_MAX;
	if (options->max_sessions == -1)
		options->max_sessions = DEFAULT_SESSIONS_MAX;
	if (options->use_dns == -1)
		options->use_dns = 1;
	if (options->client_alive_interval == -1)
		options->client_alive_interval = 0;
	if (options->client_alive_count_max == -1)
		options->client_alive_count_max = 3;
	if (options->num_authkeys_files == 0) {
		array_append(defaultkey, 0, "AuthorizedKeysFiles",
		    &options->authorized_keys_files,
		    &options->num_authkeys_files,
		    _PATH_SSH_USER_PERMITTED_KEYS);
		array_append(defaultkey, 0, "AuthorizedKeysFiles",
		    &options->authorized_keys_files,
		    &options->num_authkeys_files,
		    _PATH_SSH_USER_PERMITTED_KEYS2);
	}
	if (options->permit_tun == -1)
		options->permit_tun = SSH_TUNMODE_NO;
	if (options->ip_qos_interactive == -1)
		options->ip_qos_interactive = IPTOS_DSCP_AF21;
	if (options->ip_qos_bulk == -1)
		options->ip_qos_bulk = IPTOS_DSCP_CS1;
	if (options->version_addendum == NULL)
		options->version_addendum = xstrdup(SSH_VERSION_FREEBSD);
	if (options->fwd_opts.streamlocal_bind_mask == (mode_t)-1)
		options->fwd_opts.streamlocal_bind_mask = 0177;
	if (options->fwd_opts.streamlocal_bind_unlink == -1)
		options->fwd_opts.streamlocal_bind_unlink = 0;
	if (options->fingerprint_hash == -1)
		options->fingerprint_hash = SSH_FP_HASH_DEFAULT;
	if (options->disable_forwarding == -1)
		options->disable_forwarding = 0;
	if (options->expose_userauth_info == -1)
		options->expose_userauth_info = 0;
	if (options->use_blacklist == -1)
		options->use_blacklist = 0;

	assemble_algorithms(options);

	/* Turn privilege separation and sandboxing on by default */
	if (use_privsep == -1)
		use_privsep = PRIVSEP_ON;

#define CLEAR_ON_NONE(v) \
	do { \
		if (option_clear_or_none(v)) { \
			free(v); \
			v = NULL; \
		} \
	} while(0)
	CLEAR_ON_NONE(options->pid_file);
	CLEAR_ON_NONE(options->xauth_location);
	CLEAR_ON_NONE(options->banner);
	CLEAR_ON_NONE(options->trusted_user_ca_keys);
	CLEAR_ON_NONE(options->revoked_keys_file);
	CLEAR_ON_NONE(options->authorized_principals_file);
	CLEAR_ON_NONE(options->adm_forced_command);
	CLEAR_ON_NONE(options->chroot_directory);
	CLEAR_ON_NONE(options->routing_domain);
	for (i = 0; i < options->num_host_key_files; i++)
		CLEAR_ON_NONE(options->host_key_files[i]);
	for (i = 0; i < options->num_host_cert_files; i++)
		CLEAR_ON_NONE(options->host_cert_files[i]);
#undef CLEAR_ON_NONE

	/* Similar handling for AuthenticationMethods=any */
	if (options->num_auth_methods == 1 &&
	    strcmp(options->auth_methods[0], "any") == 0) {
		free(options->auth_methods[0]);
		options->auth_methods[0] = NULL;
		options->num_auth_methods = 0;
	}

#ifndef HAVE_MMAP
	if (use_privsep && options->compression == 1) {
		error("This platform does not support both privilege "
		    "separation and compression");
		error("Compression disabled");
		options->compression = 0;
	}
#endif

}

/* Keyword tokens. */
typedef enum {
	sBadOption,		/* == unknown option */
	/* Portable-specific options */
	sUsePAM,
	/* Standard Options */
	sPort, sHostKeyFile, sLoginGraceTime,
	sPermitRootLogin, sLogFacility, sLogLevel,
	sRhostsRSAAuthentication, sRSAAuthentication,
	sKerberosAuthentication, sKerberosOrLocalPasswd, sKerberosTicketCleanup,
	sKerberosGetAFSToken, sChallengeResponseAuthentication,
	sPasswordAuthentication, sKbdInteractiveAuthentication,
	sListenAddress, sAddressFamily,
	sPrintMotd, sPrintLastLog, sIgnoreRhosts,
	sX11Forwarding, sX11DisplayOffset, sX11UseLocalhost,
	sPermitTTY, sStrictModes, sEmptyPasswd, sTCPKeepAlive,
	sPermitUserEnvironment, sAllowTcpForwarding, sCompression,
	sRekeyLimit, sAllowUsers, sDenyUsers, sAllowGroups, sDenyGroups,
	sIgnoreUserKnownHosts, sCiphers, sMacs, sPidFile,
	sGatewayPorts, sPubkeyAuthentication, sPubkeyAcceptedKeyTypes,
	sXAuthLocation, sSubsystem, sMaxStartups, sMaxAuthTries, sMaxSessions,
	sBanner, sUseDNS, sHostbasedAuthentication,
	sHostbasedUsesNameFromPacketOnly, sHostbasedAcceptedKeyTypes,
	sHostKeyAlgorithms,
	sClientAliveInterval, sClientAliveCountMax, sAuthorizedKeysFile,
	sGssAuthentication, sGssCleanupCreds, sGssStrictAcceptor,
	sAcceptEnv, sSetEnv, sPermitTunnel,
	sMatch, sPermitOpen, sPermitListen, sForceCommand, sChrootDirectory,
	sUsePrivilegeSeparation, sAllowAgentForwarding,
	sHostCertificate,
	sRevokedKeys, sTrustedUserCAKeys, sAuthorizedPrincipalsFile,
	sAuthorizedPrincipalsCommand, sAuthorizedPrincipalsCommandUser,
	sKexAlgorithms, sIPQoS, sVersionAddendum,
	sAuthorizedKeysCommand, sAuthorizedKeysCommandUser,
	sAuthenticationMethods, sHostKeyAgent, sPermitUserRC,
	sStreamLocalBindMask, sStreamLocalBindUnlink,
	sAllowStreamLocalForwarding, sFingerprintHash, sDisableForwarding,
	sExposeAuthInfo, sRDomain,
	sUseBlacklist,
	sDeprecated, sIgnore, sUnsupported
} ServerOpCodes;

#define SSHCFG_GLOBAL	0x01	/* allowed in main section of sshd_config */
#define SSHCFG_MATCH	0x02	/* allowed inside a Match section */
#define SSHCFG_ALL	(SSHCFG_GLOBAL|SSHCFG_MATCH)

/* Textual representation of the tokens. */
static struct {
	const char *name;
	ServerOpCodes opcode;
	u_int flags;
} keywords[] = {
	/* Portable-specific options */
#ifdef USE_PAM
	{ "usepam", sUsePAM, SSHCFG_GLOBAL },
#else
	{ "usepam", sUnsupported, SSHCFG_GLOBAL },
#endif
	{ "pamauthenticationviakbdint", sDeprecated, SSHCFG_GLOBAL },
	/* Standard Options */
	{ "port", sPort, SSHCFG_GLOBAL },
	{ "hostkey", sHostKeyFile, SSHCFG_GLOBAL },
	{ "hostdsakey", sHostKeyFile, SSHCFG_GLOBAL },		/* alias */
	{ "hostkeyagent", sHostKeyAgent, SSHCFG_GLOBAL },
	{ "pidfile", sPidFile, SSHCFG_GLOBAL },
	{ "serverkeybits", sDeprecated, SSHCFG_GLOBAL },
	{ "logingracetime", sLoginGraceTime, SSHCFG_GLOBAL },
	{ "keyregenerationinterval", sDeprecated, SSHCFG_GLOBAL },
	{ "permitrootlogin", sPermitRootLogin, SSHCFG_ALL },
	{ "syslogfacility", sLogFacility, SSHCFG_GLOBAL },
	{ "loglevel", sLogLevel, SSHCFG_ALL },
	{ "rhostsauthentication", sDeprecated, SSHCFG_GLOBAL },
	{ "rhostsrsaauthentication", sDeprecated, SSHCFG_ALL },
	{ "hostbasedauthentication", sHostbasedAuthentication, SSHCFG_ALL },
	{ "hostbasedusesnamefrompacketonly", sHostbasedUsesNameFromPacketOnly, SSHCFG_ALL },
	{ "hostbasedacceptedkeytypes", sHostbasedAcceptedKeyTypes, SSHCFG_ALL },
	{ "hostkeyalgorithms", sHostKeyAlgorithms, SSHCFG_GLOBAL },
	{ "rsaauthentication", sDeprecated, SSHCFG_ALL },
	{ "pubkeyauthentication", sPubkeyAuthentication, SSHCFG_ALL },
	{ "pubkeyacceptedkeytypes", sPubkeyAcceptedKeyTypes, SSHCFG_ALL },
	{ "dsaauthentication", sPubkeyAuthentication, SSHCFG_GLOBAL }, /* alias */
#ifdef KRB5
	{ "kerberosauthentication", sKerberosAuthentication, SSHCFG_ALL },
	{ "kerberosorlocalpasswd", sKerberosOrLocalPasswd, SSHCFG_GLOBAL },
	{ "kerberosticketcleanup", sKerberosTicketCleanup, SSHCFG_GLOBAL },
#ifdef USE_AFS
	{ "kerberosgetafstoken", sKerberosGetAFSToken, SSHCFG_GLOBAL },
#else
	{ "kerberosgetafstoken", sUnsupported, SSHCFG_GLOBAL },
#endif
#else
	{ "kerberosauthentication", sUnsupported, SSHCFG_ALL },
	{ "kerberosorlocalpasswd", sUnsupported, SSHCFG_GLOBAL },
	{ "kerberosticketcleanup", sUnsupported, SSHCFG_GLOBAL },
	{ "kerberosgetafstoken", sUnsupported, SSHCFG_GLOBAL },
#endif
	{ "kerberostgtpassing", sUnsupported, SSHCFG_GLOBAL },
	{ "afstokenpassing", sUnsupported, SSHCFG_GLOBAL },
#ifdef GSSAPI
	{ "gssapiauthentication", sGssAuthentication, SSHCFG_ALL },
	{ "gssapicleanupcredentials", sGssCleanupCreds, SSHCFG_GLOBAL },
	{ "gssapistrictacceptorcheck", sGssStrictAcceptor, SSHCFG_GLOBAL },
#else
	{ "gssapiauthentication", sUnsupported, SSHCFG_ALL },
	{ "gssapicleanupcredentials", sUnsupported, SSHCFG_GLOBAL },
	{ "gssapistrictacceptorcheck", sUnsupported, SSHCFG_GLOBAL },
#endif
	{ "passwordauthentication", sPasswordAuthentication, SSHCFG_ALL },
	{ "kbdinteractiveauthentication", sKbdInteractiveAuthentication, SSHCFG_ALL },
	{ "challengeresponseauthentication", sChallengeResponseAuthentication, SSHCFG_GLOBAL },
	{ "skeyauthentication", sDeprecated, SSHCFG_GLOBAL },
	{ "checkmail", sDeprecated, SSHCFG_GLOBAL },
	{ "listenaddress", sListenAddress, SSHCFG_GLOBAL },
	{ "addressfamily", sAddressFamily, SSHCFG_GLOBAL },
	{ "printmotd", sPrintMotd, SSHCFG_GLOBAL },
#ifdef DISABLE_LASTLOG
	{ "printlastlog", sUnsupported, SSHCFG_GLOBAL },
#else
	{ "printlastlog", sPrintLastLog, SSHCFG_GLOBAL },
#endif
	{ "ignorerhosts", sIgnoreRhosts, SSHCFG_GLOBAL },
	{ "ignoreuserknownhosts", sIgnoreUserKnownHosts, SSHCFG_GLOBAL },
	{ "x11forwarding", sX11Forwarding, SSHCFG_ALL },
	{ "x11displayoffset", sX11DisplayOffset, SSHCFG_ALL },
	{ "x11uselocalhost", sX11UseLocalhost, SSHCFG_ALL },
	{ "xauthlocation", sXAuthLocation, SSHCFG_GLOBAL },
	{ "strictmodes", sStrictModes, SSHCFG_GLOBAL },
	{ "permitemptypasswords", sEmptyPasswd, SSHCFG_ALL },
	{ "permituserenvironment", sPermitUserEnvironment, SSHCFG_GLOBAL },
	{ "uselogin", sDeprecated, SSHCFG_GLOBAL },
	{ "compression", sCompression, SSHCFG_GLOBAL },
	{ "rekeylimit", sRekeyLimit, SSHCFG_ALL },
	{ "tcpkeepalive", sTCPKeepAlive, SSHCFG_GLOBAL },
	{ "keepalive", sTCPKeepAlive, SSHCFG_GLOBAL },	/* obsolete alias */
	{ "allowtcpforwarding", sAllowTcpForwarding, SSHCFG_ALL },
	{ "allowagentforwarding", sAllowAgentForwarding, SSHCFG_ALL },
	{ "allowusers", sAllowUsers, SSHCFG_ALL },
	{ "denyusers", sDenyUsers, SSHCFG_ALL },
	{ "allowgroups", sAllowGroups, SSHCFG_ALL },
	{ "denygroups", sDenyGroups, SSHCFG_ALL },
	{ "ciphers", sCiphers, SSHCFG_GLOBAL },
	{ "macs", sMacs, SSHCFG_GLOBAL },
	{ "protocol", sIgnore, SSHCFG_GLOBAL },
	{ "gatewayports", sGatewayPorts, SSHCFG_ALL },
	{ "subsystem", sSubsystem, SSHCFG_GLOBAL },
	{ "maxstartups", sMaxStartups, SSHCFG_GLOBAL },
	{ "maxauthtries", sMaxAuthTries, SSHCFG_ALL },
	{ "maxsessions", sMaxSessions, SSHCFG_ALL },
	{ "banner", sBanner, SSHCFG_ALL },
	{ "usedns", sUseDNS, SSHCFG_GLOBAL },
	{ "verifyreversemapping", sDeprecated, SSHCFG_GLOBAL },
	{ "reversemappingcheck", sDeprecated, SSHCFG_GLOBAL },
	{ "clientaliveinterval", sClientAliveInterval, SSHCFG_ALL },
	{ "clientalivecountmax", sClientAliveCountMax, SSHCFG_ALL },
	{ "authorizedkeysfile", sAuthorizedKeysFile, SSHCFG_ALL },
	{ "authorizedkeysfile2", sDeprecated, SSHCFG_ALL },
	{ "useprivilegeseparation", sDeprecated, SSHCFG_GLOBAL},
	{ "acceptenv", sAcceptEnv, SSHCFG_ALL },
	{ "setenv", sSetEnv, SSHCFG_ALL },
	{ "permittunnel", sPermitTunnel, SSHCFG_ALL },
	{ "permittty", sPermitTTY, SSHCFG_ALL },
	{ "permituserrc", sPermitUserRC, SSHCFG_ALL },
	{ "match", sMatch, SSHCFG_ALL },
	{ "permitopen", sPermitOpen, SSHCFG_ALL },
	{ "permitlisten", sPermitListen, SSHCFG_ALL },
	{ "forcecommand", sForceCommand, SSHCFG_ALL },
	{ "chrootdirectory", sChrootDirectory, SSHCFG_ALL },
	{ "hostcertificate", sHostCertificate, SSHCFG_GLOBAL },
	{ "revokedkeys", sRevokedKeys, SSHCFG_ALL },
	{ "trustedusercakeys", sTrustedUserCAKeys, SSHCFG_ALL },
	{ "authorizedprincipalsfile", sAuthorizedPrincipalsFile, SSHCFG_ALL },
	{ "kexalgorithms", sKexAlgorithms, SSHCFG_GLOBAL },
	{ "ipqos", sIPQoS, SSHCFG_ALL },
	{ "authorizedkeyscommand", sAuthorizedKeysCommand, SSHCFG_ALL },
	{ "authorizedkeyscommanduser", sAuthorizedKeysCommandUser, SSHCFG_ALL },
	{ "authorizedprincipalscommand", sAuthorizedPrincipalsCommand, SSHCFG_ALL },
	{ "authorizedprincipalscommanduser", sAuthorizedPrincipalsCommandUser, SSHCFG_ALL },
	{ "versionaddendum", sVersionAddendum, SSHCFG_GLOBAL },
	{ "authenticationmethods", sAuthenticationMethods, SSHCFG_ALL },
	{ "streamlocalbindmask", sStreamLocalBindMask, SSHCFG_ALL },
	{ "streamlocalbindunlink", sStreamLocalBindUnlink, SSHCFG_ALL },
	{ "allowstreamlocalforwarding", sAllowStreamLocalForwarding, SSHCFG_ALL },
	{ "fingerprinthash", sFingerprintHash, SSHCFG_GLOBAL },
	{ "disableforwarding", sDisableForwarding, SSHCFG_ALL },
	{ "exposeauthinfo", sExposeAuthInfo, SSHCFG_ALL },
	{ "rdomain", sRDomain, SSHCFG_ALL },
	{ "useblacklist", sUseBlacklist, SSHCFG_GLOBAL },
	{ "noneenabled", sUnsupported, SSHCFG_ALL },
	{ "hpndisabled", sDeprecated, SSHCFG_ALL },
	{ "hpnbuffersize", sDeprecated, SSHCFG_ALL },
	{ "tcprcvbufpoll", sDeprecated, SSHCFG_ALL },
	{ NULL, sBadOption, 0 }
};

static struct {
	int val;
	char *text;
} tunmode_desc[] = {
	{ SSH_TUNMODE_NO, "no" },
	{ SSH_TUNMODE_POINTOPOINT, "point-to-point" },
	{ SSH_TUNMODE_ETHERNET, "ethernet" },
	{ SSH_TUNMODE_YES, "yes" },
	{ -1, NULL }
};

/* Returns an opcode name from its number */

static const char *
lookup_opcode_name(ServerOpCodes code)
{
	u_int i;

	for (i = 0; keywords[i].name != NULL; i++)
		if (keywords[i].opcode == code)
			return(keywords[i].name);
	return "UNKNOWN";
}


/*
 * Returns the number of the token pointed to by cp or sBadOption.
 */

static ServerOpCodes
parse_token(const char *cp, const char *filename,
	    int linenum, u_int *flags)
{
	u_int i;

	for (i = 0; keywords[i].name; i++)
		if (strcasecmp(cp, keywords[i].name) == 0) {
			*flags = keywords[i].flags;
			return keywords[i].opcode;
		}

	error("%s: line %d: Bad configuration option: %s",
	    filename, linenum, cp);
	return sBadOption;
}

char *
derelativise_path(const char *path)
{
	char *expanded, *ret, cwd[PATH_MAX];

	if (strcasecmp(path, "none") == 0)
		return xstrdup("none");
	expanded = tilde_expand_filename(path, getuid());
	if (*expanded == '/')
		return expanded;
	if (getcwd(cwd, sizeof(cwd)) == NULL)
		fatal("%s: getcwd: %s", __func__, strerror(errno));
	xasprintf(&ret, "%s/%s", cwd, expanded);
	free(expanded);
	return ret;
}

static void
add_listen_addr(ServerOptions *options, const char *addr,
    const char *rdomain, int port)
{
	u_int i;

	if (port > 0)
		add_one_listen_addr(options, addr, rdomain, port);
	else {
		for (i = 0; i < options->num_ports; i++) {
			add_one_listen_addr(options, addr, rdomain,
			    options->ports[i]);
		}
	}
}

static void
add_one_listen_addr(ServerOptions *options, const char *addr,
    const char *rdomain, int port)
{
	struct addrinfo hints, *ai, *aitop;
	char strport[NI_MAXSERV];
	int gaierr;
	u_int i;

	/* Find listen_addrs entry for this rdomain */
	for (i = 0; i < options->num_listen_addrs; i++) {
		if (rdomain == NULL && options->listen_addrs[i].rdomain == NULL)
			break;
		if (rdomain == NULL || options->listen_addrs[i].rdomain == NULL)
			continue;
		if (strcmp(rdomain, options->listen_addrs[i].rdomain) == 0)
			break;
	}
	if (i >= options->num_listen_addrs) {
		/* No entry for this rdomain; allocate one */
		if (i >= INT_MAX)
			fatal("%s: too many listen addresses", __func__);
		options->listen_addrs = xrecallocarray(options->listen_addrs,
		    options->num_listen_addrs, options->num_listen_addrs + 1,
		    sizeof(*options->listen_addrs));
		i = options->num_listen_addrs++;
		if (rdomain != NULL)
			options->listen_addrs[i].rdomain = xstrdup(rdomain);
	}
	/* options->listen_addrs[i] points to the addresses for this rdomain */

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = options->address_family;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = (addr == NULL) ? AI_PASSIVE : 0;
	snprintf(strport, sizeof strport, "%d", port);
	if ((gaierr = getaddrinfo(addr, strport, &hints, &aitop)) != 0)
		fatal("bad addr or host: %s (%s)",
		    addr ? addr : "<NULL>",
		    ssh_gai_strerror(gaierr));
	for (ai = aitop; ai->ai_next; ai = ai->ai_next)
		;
	ai->ai_next = options->listen_addrs[i].addrs;
	options->listen_addrs[i].addrs = aitop;
}

/* Returns nonzero if the routing domain name is valid */
static int
valid_rdomain(const char *name)
{
#if defined(HAVE_SYS_VALID_RDOMAIN)
	return sys_valid_rdomain(name);
#elif defined(__OpenBSD__)
	const char *errstr;
	long long num;
	struct rt_tableinfo info;
	int mib[6];
	size_t miblen = sizeof(mib);

	if (name == NULL)
		return 1;

	num = strtonum(name, 0, 255, &errstr);
	if (errstr != NULL)
		return 0;

	/* Check whether the table actually exists */
	memset(mib, 0, sizeof(mib));
	mib[0] = CTL_NET;
	mib[1] = PF_ROUTE;
	mib[4] = NET_RT_TABLE;
	mib[5] = (int)num;
	if (sysctl(mib, 6, &info, &miblen, NULL, 0) == -1)
		return 0;

	return 1;
#else /* defined(__OpenBSD__) */
	error("Routing domains are not supported on this platform");
	return 0;
#endif
}

/*
 * Queue a ListenAddress to be processed once we have all of the Ports
 * and AddressFamily options.
 */
static void
queue_listen_addr(ServerOptions *options, const char *addr,
    const char *rdomain, int port)
{
	struct queued_listenaddr *qla;

	options->queued_listen_addrs = xrecallocarray(
	    options->queued_listen_addrs,
	    options->num_queued_listens, options->num_queued_listens + 1,
	    sizeof(*options->queued_listen_addrs));
	qla = &options->queued_listen_addrs[options->num_queued_listens++];
	qla->addr = xstrdup(addr);
	qla->port = port;
	qla->rdomain = rdomain == NULL ? NULL : xstrdup(rdomain);
}

/*
 * Process queued (text) ListenAddress entries.
 */
static void
process_queued_listen_addrs(ServerOptions *options)
{
	u_int i;
	struct queued_listenaddr *qla;

	if (options->num_ports == 0)
		options->ports[options->num_ports++] = SSH_DEFAULT_PORT;
	if (options->address_family == -1)
		options->address_family = AF_UNSPEC;

	for (i = 0; i < options->num_queued_listens; i++) {
		qla = &options->queued_listen_addrs[i];
		add_listen_addr(options, qla->addr, qla->rdomain, qla->port);
		free(qla->addr);
		free(qla->rdomain);
	}
	free(options->queued_listen_addrs);
	options->queued_listen_addrs = NULL;
	options->num_queued_listens = 0;
}

/*
 * Inform channels layer of permitopen options for a single forwarding
 * direction (local/remote).
 */
static void
process_permitopen_list(struct ssh *ssh, ServerOpCodes opcode,
    char **opens, u_int num_opens)
{
	u_int i;
	int port;
	char *host, *arg, *oarg;
	int where = opcode == sPermitOpen ? FORWARD_LOCAL : FORWARD_REMOTE;
	const char *what = lookup_opcode_name(opcode);

	channel_clear_permission(ssh, FORWARD_ADM, where);
	if (num_opens == 0)
		return; /* permit any */

	/* handle keywords: "any" / "none" */
	if (num_opens == 1 && strcmp(opens[0], "any") == 0)
		return;
	if (num_opens == 1 && strcmp(opens[0], "none") == 0) {
		channel_disable_admin(ssh, where);
		return;
	}
	/* Otherwise treat it as a list of permitted host:port */
	for (i = 0; i < num_opens; i++) {
		oarg = arg = xstrdup(opens[i]);
		host = hpdelim(&arg);
		if (host == NULL)
			fatal("%s: missing host in %s", __func__, what);
		host = cleanhostname(host);
		if (arg == NULL || ((port = permitopen_port(arg)) < 0))
			fatal("%s: bad port number in %s", __func__, what);
		/* Send it to channels layer */
		channel_add_permission(ssh, FORWARD_ADM,
		    where, host, port);
		free(oarg);
	}
}

/*
 * Inform channels layer of permitopen options from configuration.
 */
void
process_permitopen(struct ssh *ssh, ServerOptions *options)
{
	process_permitopen_list(ssh, sPermitOpen,
	    options->permitted_opens, options->num_permitted_opens);
	process_permitopen_list(ssh, sPermitListen,
	    options->permitted_listens,
	    options->num_permitted_listens);
}

struct connection_info *
get_connection_info(int populate, int use_dns)
{
	struct ssh *ssh = active_state; /* XXX */
	static struct connection_info ci;

	if (!populate)
		return &ci;
	ci.host = auth_get_canonical_hostname(ssh, use_dns);
	ci.address = ssh_remote_ipaddr(ssh);
	ci.laddress = ssh_local_ipaddr(ssh);
	ci.lport = ssh_local_port(ssh);
	ci.rdomain = ssh_packet_rdomain_in(ssh);
	return &ci;
}

/*
 * The strategy for the Match blocks is that the config file is parsed twice.
 *
 * The first time is at startup.  activep is initialized to 1 and the
 * directives in the global context are processed and acted on.  Hitting a
 * Match directive unsets activep and the directives inside the block are
 * checked for syntax only.
 *
 * The second time is after a connection has been established but before
 * authentication.  activep is initialized to 2 and global config directives
 * are ignored since they have already been processed.  If the criteria in a
 * Match block is met, activep is set and the subsequent directives
 * processed and actioned until EOF or another Match block unsets it.  Any
 * options set are copied into the main server config.
 *
 * Potential additions/improvements:
 *  - Add Match support for pre-kex directives, eg. Ciphers.
 *
 *  - Add a Tag directive (idea from David Leonard) ala pf, eg:
 *	Match Address 192.168.0.*
 *		Tag trusted
 *	Match Group wheel
 *		Tag trusted
 *	Match Tag trusted
 *		AllowTcpForwarding yes
 *		GatewayPorts clientspecified
 *		[...]
 *
 *  - Add a PermittedChannelRequests directive
 *	Match Group shell
 *		PermittedChannelRequests session,forwarded-tcpip
 */

static int
match_cfg_line_group(const char *grps, int line, const char *user)
{
	int result = 0;
	struct passwd *pw;

	if (user == NULL)
		goto out;

	if ((pw = getpwnam(user)) == NULL) {
		debug("Can't match group at line %d because user %.100s does "
		    "not exist", line, user);
	} else if (ga_init(pw->pw_name, pw->pw_gid) == 0) {
		debug("Can't Match group because user %.100s not in any group "
		    "at line %d", user, line);
	} else if (ga_match_pattern_list(grps) != 1) {
		debug("user %.100s does not match group list %.100s at line %d",
		    user, grps, line);
	} else {
		debug("user %.100s matched group list %.100s at line %d", user,
		    grps, line);
		result = 1;
	}
out:
	ga_free();
	return result;
}

static void
match_test_missing_fatal(const char *criteria, const char *attrib)
{
	fatal("'Match %s' in configuration but '%s' not in connection "
	    "test specification.", criteria, attrib);
}

/*
 * All of the attributes on a single Match line are ANDed together, so we need
 * to check every attribute and set the result to zero if any attribute does
 * not match.
 */
static int
match_cfg_line(char **condition, int line, struct connection_info *ci)
{
	int result = 1, attributes = 0, port;
	char *arg, *attrib, *cp = *condition;

	if (ci == NULL)
		debug3("checking syntax for 'Match %s'", cp);
	else
		debug3("checking match for '%s' user %s host %s addr %s "
		    "laddr %s lport %d", cp, ci->user ? ci->user : "(null)",
		    ci->host ? ci->host : "(null)",
		    ci->address ? ci->address : "(null)",
		    ci->laddress ? ci->laddress : "(null)", ci->lport);

	while ((attrib = strdelim(&cp)) && *attrib != '\0') {
		attributes++;
		if (strcasecmp(attrib, "all") == 0) {
			if (attributes != 1 ||
			    ((arg = strdelim(&cp)) != NULL && *arg != '\0')) {
				error("'all' cannot be combined with other "
				    "Match attributes");
				return -1;
			}
			*condition = cp;
			return 1;
		}
		if ((arg = strdelim(&cp)) == NULL || *arg == '\0') {
			error("Missing Match criteria for %s", attrib);
			return -1;
		}
		if (strcasecmp(attrib, "user") == 0) {
			if (ci == NULL) {
				result = 0;
				continue;
			}
			if (ci->user == NULL)
				match_test_missing_fatal("User", "user");
			if (match_pattern_list(ci->user, arg, 0) != 1)
				result = 0;
			else
				debug("user %.100s matched 'User %.100s' at "
				    "line %d", ci->user, arg, line);
		} else if (strcasecmp(attrib, "group") == 0) {
			if (ci == NULL) {
				result = 0;
				continue;
			}
			if (ci->user == NULL)
				match_test_missing_fatal("Group", "user");
			switch (match_cfg_line_group(arg, line, ci->user)) {
			case -1:
				return -1;
			case 0:
				result = 0;
			}
		} else if (strcasecmp(attrib, "host") == 0) {
			if (ci == NULL) {
				result = 0;
				continue;
			}
			if (ci->host == NULL)
				match_test_missing_fatal("Host", "host");
			if (match_hostname(ci->host, arg) != 1)
				result = 0;
			else
				debug("connection from %.100s matched 'Host "
				    "%.100s' at line %d", ci->host, arg, line);
		} else if (strcasecmp(attrib, "address") == 0) {
			if (ci == NULL) {
				result = 0;
				continue;
			}
			if (ci->address == NULL)
				match_test_missing_fatal("Address", "addr");
			switch (addr_match_list(ci->address, arg)) {
			case 1:
				debug("connection from %.100s matched 'Address "
				    "%.100s' at line %d", ci->address, arg, line);
				break;
			case 0:
			case -1:
				result = 0;
				break;
			case -2:
				return -1;
			}
		} else if (strcasecmp(attrib, "localaddress") == 0){
			if (ci == NULL) {
				result = 0;
				continue;
			}
			if (ci->laddress == NULL)
				match_test_missing_fatal("LocalAddress",
				    "laddr");
			switch (addr_match_list(ci->laddress, arg)) {
			case 1:
				debug("connection from %.100s matched "
				    "'LocalAddress %.100s' at line %d",
				    ci->laddress, arg, line);
				break;
			case 0:
			case -1:
				result = 0;
				break;
			case -2:
				return -1;
			}
		} else if (strcasecmp(attrib, "localport") == 0) {
			if ((port = a2port(arg)) == -1) {
				error("Invalid LocalPort '%s' on Match line",
				    arg);
				return -1;
			}
			if (ci == NULL) {
				result = 0;
				continue;
			}
			if (ci->lport == 0)
				match_test_missing_fatal("LocalPort", "lport");
			/* TODO support port lists */
			if (port == ci->lport)
				debug("connection from %.100s matched "
				    "'LocalPort %d' at line %d",
				    ci->laddress, port, line);
			else
				result = 0;
		} else if (strcasecmp(attrib, "rdomain") == 0) {
			if (ci == NULL || ci->rdomain == NULL) {
				result = 0;
				continue;
			}
			if (match_pattern_list(ci->rdomain, arg, 0) != 1)
				result = 0;
			else
				debug("user %.100s matched 'RDomain %.100s' at "
				    "line %d", ci->rdomain, arg, line);
		} else {
			error("Unsupported Match attribute %s", attrib);
			return -1;
		}
	}
	if (attributes == 0) {
		error("One or more attributes required for Match");
		return -1;
	}
	if (ci != NULL)
		debug3("match %sfound", result ? "" : "not ");
	*condition = cp;
	return result;
}

#define WHITESPACE " \t\r\n"

/* Multistate option parsing */
struct multistate {
	char *key;
	int value;
};
static const struct multistate multistate_flag[] = {
	{ "yes",			1 },
	{ "no",				0 },
	{ NULL, -1 }
};
static const struct multistate multistate_addressfamily[] = {
	{ "inet",			AF_INET },
	{ "inet6",			AF_INET6 },
	{ "any",			AF_UNSPEC },
	{ NULL, -1 }
};
static const struct multistate multistate_permitrootlogin[] = {
	{ "without-password",		PERMIT_NO_PASSWD },
	{ "prohibit-password",		PERMIT_NO_PASSWD },
	{ "forced-commands-only",	PERMIT_FORCED_ONLY },
	{ "yes",			PERMIT_YES },
	{ "no",				PERMIT_NO },
	{ NULL, -1 }
};
static const struct multistate multistate_compression[] = {
	{ "yes",			COMP_DELAYED },
	{ "delayed",			COMP_DELAYED },
	{ "no",				COMP_NONE },
	{ NULL, -1 }
};
static const struct multistate multistate_gatewayports[] = {
	{ "clientspecified",		2 },
	{ "yes",			1 },
	{ "no",				0 },
	{ NULL, -1 }
};
static const struct multistate multistate_tcpfwd[] = {
	{ "yes",			FORWARD_ALLOW },
	{ "all",			FORWARD_ALLOW },
	{ "no",				FORWARD_DENY },
	{ "remote",			FORWARD_REMOTE },
	{ "local",			FORWARD_LOCAL },
	{ NULL, -1 }
};

int
process_server_config_line(ServerOptions *options, char *line,
    const char *filename, int linenum, int *activep,
    struct connection_info *connectinfo)
{
	char *cp, ***chararrayptr, **charptr, *arg, *arg2, *p;
	int cmdline = 0, *intptr, value, value2, n, port;
	SyslogFacility *log_facility_ptr;
	LogLevel *log_level_ptr;
	ServerOpCodes opcode;
	u_int i, *uintptr, uvalue, flags = 0;
	size_t len;
	long long val64;
	const struct multistate *multistate_ptr;
	const char *errstr;

	/* Strip trailing whitespace. Allow \f (form feed) at EOL only */
	if ((len = strlen(line)) == 0)
		return 0;
	for (len--; len > 0; len--) {
		if (strchr(WHITESPACE "\f", line[len]) == NULL)
			break;
		line[len] = '\0';
	}

	cp = line;
	if ((arg = strdelim(&cp)) == NULL)
		return 0;
	/* Ignore leading whitespace */
	if (*arg == '\0')
		arg = strdelim(&cp);
	if (!arg || !*arg || *arg == '#')
		return 0;
	intptr = NULL;
	charptr = NULL;
	opcode = parse_token(arg, filename, linenum, &flags);

	if (activep == NULL) { /* We are processing a command line directive */
		cmdline = 1;
		activep = &cmdline;
	}
	if (*activep && opcode != sMatch)
		debug3("%s:%d setting %s %s", filename, linenum, arg, cp);
	if (*activep == 0 && !(flags & SSHCFG_MATCH)) {
		if (connectinfo == NULL) {
			fatal("%s line %d: Directive '%s' is not allowed "
			    "within a Match block", filename, linenum, arg);
		} else { /* this is a directive we have already processed */
			while (arg)
				arg = strdelim(&cp);
			return 0;
		}
	}

	switch (opcode) {
	/* Portable-specific options */
	case sUsePAM:
		intptr = &options->use_pam;
		goto parse_flag;

	/* Standard Options */
	case sBadOption:
		return -1;
	case sPort:
		/* ignore ports from configfile if cmdline specifies ports */
		if (options->ports_from_cmdline)
			return 0;
		if (options->num_ports >= MAX_PORTS)
			fatal("%s line %d: too many ports.",
			    filename, linenum);
		arg = strdelim(&cp);
		if (!arg || *arg == '\0')
			fatal("%s line %d: missing port number.",
			    filename, linenum);
		options->ports[options->num_ports++] = a2port(arg);
		if (options->ports[options->num_ports-1] <= 0)
			fatal("%s line %d: Badly formatted port number.",
			    filename, linenum);
		break;

	case sLoginGraceTime:
		intptr = &options->login_grace_time;
 parse_time:
		arg = strdelim(&cp);
		if (!arg || *arg == '\0')
			fatal("%s line %d: missing time value.",
			    filename, linenum);
		if ((value = convtime(arg)) == -1)
			fatal("%s line %d: invalid time value.",
			    filename, linenum);
		if (*activep && *intptr == -1)
			*intptr = value;
		break;

	case sListenAddress:
		arg = strdelim(&cp);
		if (arg == NULL || *arg == '\0')
			fatal("%s line %d: missing address",
			    filename, linenum);
		/* check for bare IPv6 address: no "[]" and 2 or more ":" */
		if (strchr(arg, '[') == NULL && (p = strchr(arg, ':')) != NULL
		    && strchr(p+1, ':') != NULL) {
			port = 0;
			p = arg;
		} else {
			p = hpdelim(&arg);
			if (p == NULL)
				fatal("%s line %d: bad address:port usage",
				    filename, linenum);
			p = cleanhostname(p);
			if (arg == NULL)
				port = 0;
			else if ((port = a2port(arg)) <= 0)
				fatal("%s line %d: bad port number",
				    filename, linenum);
		}
		/* Optional routing table */
		arg2 = NULL;
		if ((arg = strdelim(&cp)) != NULL) {
			if (strcmp(arg, "rdomain") != 0 ||
			    (arg2 = strdelim(&cp)) == NULL)
				fatal("%s line %d: bad ListenAddress syntax",
				    filename, linenum);
			if (!valid_rdomain(arg2))
				fatal("%s line %d: bad routing domain",
				    filename, linenum);
		}

		queue_listen_addr(options, p, arg2, port);

		break;

	case sAddressFamily:
		intptr = &options->address_family;
		multistate_ptr = multistate_addressfamily;
 parse_multistate:
		arg = strdelim(&cp);
		if (!arg || *arg == '\0')
			fatal("%s line %d: missing argument.",
			    filename, linenum);
		value = -1;
		for (i = 0; multistate_ptr[i].key != NULL; i++) {
			if (strcasecmp(arg, multistate_ptr[i].key) == 0) {
				value = multistate_ptr[i].value;
				break;
			}
		}
		if (value == -1)
			fatal("%s line %d: unsupported option \"%s\".",
			    filename, linenum, arg);
		if (*activep && *intptr == -1)
			*intptr = value;
		break;

	case sHostKeyFile:
		arg = strdelim(&cp);
		if (!arg || *arg == '\0')
			fatal("%s line %d: missing file name.",
			    filename, linenum);
		if (*activep)
			servconf_add_hostkey(filename, linenum, options, arg);
		break;

	case sHostKeyAgent:
		charptr = &options->host_key_agent;
		arg = strdelim(&cp);
		if (!arg || *arg == '\0')
			fatal("%s line %d: missing socket name.",
			    filename, linenum);
		if (*activep && *charptr == NULL)
			*charptr = !strcmp(arg, SSH_AUTHSOCKET_ENV_NAME) ?
			    xstrdup(arg) : derelativise_path(arg);
		break;

	case sHostCertificate:
		arg = strdelim(&cp);
		if (!arg || *arg == '\0')
			fatal("%s line %d: missing file name.",
			    filename, linenum);
		if (*activep)
			servconf_add_hostcert(filename, linenum, options, arg);
		break;

	case sPidFile:
		charptr = &options->pid_file;
 parse_filename:
		arg = strdelim(&cp);
		if (!arg || *arg == '\0')
			fatal("%s line %d: missing file name.",
			    filename, linenum);
		if (*activep && *charptr == NULL) {
			*charptr = derelativise_path(arg);
			/* increase optional counter */
			if (intptr != NULL)
				*intptr = *intptr + 1;
		}
		break;

	case sPermitRootLogin:
		intptr = &options->permit_root_login;
		multistate_ptr = multistate_permitrootlogin;
		goto parse_multistate;

	case sIgnoreRhosts:
		intptr = &options->ignore_rhosts;
 parse_flag:
		multistate_ptr = multistate_flag;
		goto parse_multistate;

	case sIgnoreUserKnownHosts:
		intptr = &options->ignore_user_known_hosts;
		goto parse_flag;

	case sHostbasedAuthentication:
		intptr = &options->hostbased_authentication;
		goto parse_flag;

	case sHostbasedUsesNameFromPacketOnly:
		intptr = &options->hostbased_uses_name_from_packet_only;
		goto parse_flag;

	case sHostbasedAcceptedKeyTypes:
		charptr = &options->hostbased_key_types;
 parse_keytypes:
		arg = strdelim(&cp);
		if (!arg || *arg == '\0')
			fatal("%s line %d: Missing argument.",
			    filename, linenum);
		if (*arg != '-' &&
		    !sshkey_names_valid2(*arg == '+' ? arg + 1 : arg, 1))
			fatal("%s line %d: Bad key types '%s'.",
			    filename, linenum, arg ? arg : "<NONE>");
		if (*activep && *charptr == NULL)
			*charptr = xstrdup(arg);
		break;

	case sHostKeyAlgorithms:
		charptr = &options->hostkeyalgorithms;
		goto parse_keytypes;

	case sPubkeyAuthentication:
		intptr = &options->pubkey_authentication;
		goto parse_flag;

	case sPubkeyAcceptedKeyTypes:
		charptr = &options->pubkey_key_types;
		goto parse_keytypes;

	case sKerberosAuthentication:
		intptr = &options->kerberos_authentication;
		goto parse_flag;

	case sKerberosOrLocalPasswd:
		intptr = &options->kerberos_or_local_passwd;
		goto parse_flag;

	case sKerberosTicketCleanup:
		intptr = &options->kerberos_ticket_cleanup;
		goto parse_flag;

	case sKerberosGetAFSToken:
		intptr = &options->kerberos_get_afs_token;
		goto parse_flag;

	case sGssAuthentication:
		intptr = &options->gss_authentication;
		goto parse_flag;

	case sGssCleanupCreds:
		intptr = &options->gss_cleanup_creds;
		goto parse_flag;

	case sGssStrictAcceptor:
		intptr = &options->gss_strict_acceptor;
		goto parse_flag;

	case sPasswordAuthentication:
		intptr = &options->password_authentication;
		goto parse_flag;

	case sKbdInteractiveAuthentication:
		intptr = &options->kbd_interactive_authentication;
		goto parse_flag;

	case sChallengeResponseAuthentication:
		intptr = &options->challenge_response_authentication;
		goto parse_flag;

	case sPrintMotd:
		intptr = &options->print_motd;
		goto parse_flag;

	case sPrintLastLog:
		intptr = &options->print_lastlog;
		goto parse_flag;

	case sX11Forwarding:
		intptr = &options->x11_forwarding;
		goto parse_flag;

	case sX11DisplayOffset:
		intptr = &options->x11_display_offset;
 parse_int:
		arg = strdelim(&cp);
		if ((errstr = atoi_err(arg, &value)) != NULL)
			fatal("%s line %d: integer value %s.",
			    filename, linenum, errstr);
		if (*activep && *intptr == -1)
			*intptr = value;
		break;

	case sX11UseLocalhost:
		intptr = &options->x11_use_localhost;
		goto parse_flag;

	case sXAuthLocation:
		charptr = &options->xauth_location;
		goto parse_filename;

	case sPermitTTY:
		intptr = &options->permit_tty;
		goto parse_flag;

	case sPermitUserRC:
		intptr = &options->permit_user_rc;
		goto parse_flag;

	case sStrictModes:
		intptr = &options->strict_modes;
		goto parse_flag;

	case sTCPKeepAlive:
		intptr = &options->tcp_keep_alive;
		goto parse_flag;

	case sEmptyPasswd:
		intptr = &options->permit_empty_passwd;
		goto parse_flag;

	case sPermitUserEnvironment:
		intptr = &options->permit_user_env;
		charptr = &options->permit_user_env_whitelist;
		arg = strdelim(&cp);
		if (!arg || *arg == '\0')
			fatal("%s line %d: missing argument.",
			    filename, linenum);
		value = 0;
		p = NULL;
		if (strcmp(arg, "yes") == 0)
			value = 1;
		else if (strcmp(arg, "no") == 0)
			value = 0;
		else {
			/* Pattern-list specified */
			value = 1;
			p = xstrdup(arg);
		}
		if (*activep && *intptr == -1) {
			*intptr = value;
			*charptr = p;
			p = NULL;
		}
		free(p);
		break;

	case sCompression:
		intptr = &options->compression;
		multistate_ptr = multistate_compression;
		goto parse_multistate;

	case sRekeyLimit:
		arg = strdelim(&cp);
		if (!arg || *arg == '\0')
			fatal("%.200s line %d: Missing argument.", filename,
			    linenum);
		if (strcmp(arg, "default") == 0) {
			val64 = 0;
		} else {
			if (scan_scaled(arg, &val64) == -1)
				fatal("%.200s line %d: Bad number '%s': %s",
				    filename, linenum, arg, strerror(errno));
			if (val64 != 0 && val64 < 16)
				fatal("%.200s line %d: RekeyLimit too small",
				    filename, linenum);
		}
		if (*activep && options->rekey_limit == -1)
			options->rekey_limit = val64;
		if (cp != NULL) { /* optional rekey interval present */
			if (strcmp(cp, "none") == 0) {
				(void)strdelim(&cp);	/* discard */
				break;
			}
			intptr = &options->rekey_interval;
			goto parse_time;
		}
		break;

	case sGatewayPorts:
		intptr = &options->fwd_opts.gateway_ports;
		multistate_ptr = multistate_gatewayports;
		goto parse_multistate;

	case sUseDNS:
		intptr = &options->use_dns;
		goto parse_flag;

	case sLogFacility:
		log_facility_ptr = &options->log_facility;
		arg = strdelim(&cp);
		value = log_facility_number(arg);
		if (value == SYSLOG_FACILITY_NOT_SET)
			fatal("%.200s line %d: unsupported log facility '%s'",
			    filename, linenum, arg ? arg : "<NONE>");
		if (*log_facility_ptr == -1)
			*log_facility_ptr = (SyslogFacility) value;
		break;

	case sLogLevel:
		log_level_ptr = &options->log_level;
		arg = strdelim(&cp);
		value = log_level_number(arg);
		if (value == SYSLOG_LEVEL_NOT_SET)
			fatal("%.200s line %d: unsupported log level '%s'",
			    filename, linenum, arg ? arg : "<NONE>");
		if (*activep && *log_level_ptr == -1)
			*log_level_ptr = (LogLevel) value;
		break;

	case sAllowTcpForwarding:
		intptr = &options->allow_tcp_forwarding;
		multistate_ptr = multistate_tcpfwd;
		goto parse_multistate;

	case sAllowStreamLocalForwarding:
		intptr = &options->allow_streamlocal_forwarding;
		multistate_ptr = multistate_tcpfwd;
		goto parse_multistate;

	case sAllowAgentForwarding:
		intptr = &options->allow_agent_forwarding;
		goto parse_flag;

	case sDisableForwarding:
		intptr = &options->disable_forwarding;
		goto parse_flag;

	case sAllowUsers:
		while ((arg = strdelim(&cp)) && *arg != '\0') {
			if (match_user(NULL, NULL, NULL, arg) == -1)
				fatal("%s line %d: invalid AllowUsers pattern: "
				    "\"%.100s\"", filename, linenum, arg);
			if (!*activep)
				continue;
			array_append(filename, linenum, "AllowUsers",
			    &options->allow_users, &options->num_allow_users,
			    arg);
		}
		break;

	case sDenyUsers:
		while ((arg = strdelim(&cp)) && *arg != '\0') {
			if (match_user(NULL, NULL, NULL, arg) == -1)
				fatal("%s line %d: invalid DenyUsers pattern: "
				    "\"%.100s\"", filename, linenum, arg);
			if (!*activep)
				continue;
			array_append(filename, linenum, "DenyUsers",
			    &options->deny_users, &options->num_deny_users,
			    arg);
		}
		break;

	case sAllowGroups:
		while ((arg = strdelim(&cp)) && *arg != '\0') {
			if (!*activep)
				continue;
			array_append(filename, linenum, "AllowGroups",
			    &options->allow_groups, &options->num_allow_groups,
			    arg);
		}
		break;

	case sDenyGroups:
		while ((arg = strdelim(&cp)) && *arg != '\0') {
			if (!*activep)
				continue;
			array_append(filename, linenum, "DenyGroups",
			    &options->deny_groups, &options->num_deny_groups,
			    arg);
		}
		break;

	case sCiphers:
		arg = strdelim(&cp);
		if (!arg || *arg == '\0')
			fatal("%s line %d: Missing argument.", filename, linenum);
		if (*arg != '-' && !ciphers_valid(*arg == '+' ? arg + 1 : arg))
			fatal("%s line %d: Bad SSH2 cipher spec '%s'.",
			    filename, linenum, arg ? arg : "<NONE>");
		if (options->ciphers == NULL)
			options->ciphers = xstrdup(arg);
		break;

	case sMacs:
		arg = strdelim(&cp);
		if (!arg || *arg == '\0')
			fatal("%s line %d: Missing argument.", filename, linenum);
		if (*arg != '-' && !mac_valid(*arg == '+' ? arg + 1 : arg))
			fatal("%s line %d: Bad SSH2 mac spec '%s'.",
			    filename, linenum, arg ? arg : "<NONE>");
		if (options->macs == NULL)
			options->macs = xstrdup(arg);
		break;

	case sKexAlgorithms:
		arg = strdelim(&cp);
		if (!arg || *arg == '\0')
			fatal("%s line %d: Missing argument.",
			    filename, linenum);
		if (*arg != '-' &&
		    !kex_names_valid(*arg == '+' ? arg + 1 : arg))
			fatal("%s line %d: Bad SSH2 KexAlgorithms '%s'.",
			    filename, linenum, arg ? arg : "<NONE>");
		if (options->kex_algorithms == NULL)
			options->kex_algorithms = xstrdup(arg);
		break;

	case sSubsystem:
		if (options->num_subsystems >= MAX_SUBSYSTEMS) {
			fatal("%s line %d: too many subsystems defined.",
			    filename, linenum);
		}
		arg = strdelim(&cp);
		if (!arg || *arg == '\0')
			fatal("%s line %d: Missing subsystem name.",
			    filename, linenum);
		if (!*activep) {
			arg = strdelim(&cp);
			break;
		}
		for (i = 0; i < options->num_subsystems; i++)
			if (strcmp(arg, options->subsystem_name[i]) == 0)
				fatal("%s line %d: Subsystem '%s' already defined.",
				    filename, linenum, arg);
		options->subsystem_name[options->num_subsystems] = xstrdup(arg);
		arg = strdelim(&cp);
		if (!arg || *arg == '\0')
			fatal("%s line %d: Missing subsystem command.",
			    filename, linenum);
		options->subsystem_command[options->num_subsystems] = xstrdup(arg);

		/* Collect arguments (separate to executable) */
		p = xstrdup(arg);
		len = strlen(p) + 1;
		while ((arg = strdelim(&cp)) != NULL && *arg != '\0') {
			len += 1 + strlen(arg);
			p = xreallocarray(p, 1, len);
			strlcat(p, " ", len);
			strlcat(p, arg, len);
		}
		options->subsystem_args[options->num_subsystems] = p;
		options->num_subsystems++;
		break;

	case sMaxStartups:
		arg = strdelim(&cp);
		if (!arg || *arg == '\0')
			fatal("%s line %d: Missing MaxStartups spec.",
			    filename, linenum);
		if ((n = sscanf(arg, "%d:%d:%d",
		    &options->max_startups_begin,
		    &options->max_startups_rate,
		    &options->max_startups)) == 3) {
			if (options->max_startups_begin >
			    options->max_startups ||
			    options->max_startups_rate > 100 ||
			    options->max_startups_rate < 1)
				fatal("%s line %d: Illegal MaxStartups spec.",
				    filename, linenum);
		} else if (n != 1)
			fatal("%s line %d: Illegal MaxStartups spec.",
			    filename, linenum);
		else
			options->max_startups = options->max_startups_begin;
		break;

	case sMaxAuthTries:
		intptr = &options->max_authtries;
		goto parse_int;

	case sMaxSessions:
		intptr = &options->max_sessions;
		goto parse_int;

	case sBanner:
		charptr = &options->banner;
		goto parse_filename;

	/*
	 * These options can contain %X options expanded at
	 * connect time, so that you can specify paths like:
	 *
	 * AuthorizedKeysFile	/etc/ssh_keys/%u
	 */
	case sAuthorizedKeysFile:
		if (*activep && options->num_authkeys_files == 0) {
			while ((arg = strdelim(&cp)) && *arg != '\0') {
				arg = tilde_expand_filename(arg, getuid());
				array_append(filename, linenum,
				    "AuthorizedKeysFile",
				    &options->authorized_keys_files,
				    &options->num_authkeys_files, arg);
				free(arg);
			}
		}
		return 0;

	case sAuthorizedPrincipalsFile:
		charptr = &options->authorized_principals_file;
		arg = strdelim(&cp);
		if (!arg || *arg == '\0')
			fatal("%s line %d: missing file name.",
			    filename, linenum);
		if (*activep && *charptr == NULL) {
			*charptr = tilde_expand_filename(arg, getuid());
			/* increase optional counter */
			if (intptr != NULL)
				*intptr = *intptr + 1;
		}
		break;

	case sClientAliveInterval:
		intptr = &options->client_alive_interval;
		goto parse_time;

	case sClientAliveCountMax:
		intptr = &options->client_alive_count_max;
		goto parse_int;

	case sAcceptEnv:
		while ((arg = strdelim(&cp)) && *arg != '\0') {
			if (strchr(arg, '=') != NULL)
				fatal("%s line %d: Invalid environment name.",
				    filename, linenum);
			if (!*activep)
				continue;
			array_append(filename, linenum, "AcceptEnv",
			    &options->accept_env, &options->num_accept_env,
			    arg);
		}
		break;

	case sSetEnv:
		uvalue = options->num_setenv;
		while ((arg = strdelimw(&cp)) && *arg != '\0') {
			if (strchr(arg, '=') == NULL)
				fatal("%s line %d: Invalid environment.",
				    filename, linenum);
			if (!*activep || uvalue != 0)
				continue;
			array_append(filename, linenum, "SetEnv",
			    &options->setenv, &options->num_setenv, arg);
		}
		break;

	case sPermitTunnel:
		intptr = &options->permit_tun;
		arg = strdelim(&cp);
		if (!arg || *arg == '\0')
			fatal("%s line %d: Missing yes/point-to-point/"
			    "ethernet/no argument.", filename, linenum);
		value = -1;
		for (i = 0; tunmode_desc[i].val != -1; i++)
			if (strcmp(tunmode_desc[i].text, arg) == 0) {
				value = tunmode_desc[i].val;
				break;
			}
		if (value == -1)
			fatal("%s line %d: Bad yes/point-to-point/ethernet/"
			    "no argument: %s", filename, linenum, arg);
		if (*activep && *intptr == -1)
			*intptr = value;
		break;

	case sMatch:
		if (cmdline)
			fatal("Match directive not supported as a command-line "
			   "option");
		value = match_cfg_line(&cp, linenum, connectinfo);
		if (value < 0)
			fatal("%s line %d: Bad Match condition", filename,
			    linenum);
		*activep = value;
		break;

	case sPermitListen:
	case sPermitOpen:
		if (opcode == sPermitListen) {
			uintptr = &options->num_permitted_listens;
			chararrayptr = &options->permitted_listens;
		} else {
			uintptr = &options->num_permitted_opens;
			chararrayptr = &options->permitted_opens;
		}
		arg = strdelim(&cp);
		if (!arg || *arg == '\0')
			fatal("%s line %d: missing %s specification",
			    filename, linenum, lookup_opcode_name(opcode));
		uvalue = *uintptr;	/* modified later */
		if (strcmp(arg, "any") == 0 || strcmp(arg, "none") == 0) {
			if (*activep && uvalue == 0) {
				*uintptr = 1;
				*chararrayptr = xcalloc(1,
				    sizeof(**chararrayptr));
				(*chararrayptr)[0] = xstrdup(arg);
			}
			break;
		}
		for (; arg != NULL && *arg != '\0'; arg = strdelim(&cp)) {
			if (opcode == sPermitListen &&
			    strchr(arg, ':') == NULL) {
				/*
				 * Allow bare port number for PermitListen
				 * to indicate a wildcard listen host.
				 */
				xasprintf(&arg2, "*:%s", arg);
			} else {
				arg2 = xstrdup(arg);
				p = hpdelim(&arg);
				if (p == NULL) {
					fatal("%s line %d: missing host in %s",
					    filename, linenum,
					    lookup_opcode_name(opcode));
				}
				p = cleanhostname(p);
			}
			if (arg == NULL ||
			    ((port = permitopen_port(arg)) < 0)) {
				fatal("%s line %d: bad port number in %s",
				    filename, linenum,
				    lookup_opcode_name(opcode));
			}
			if (*activep && uvalue == 0) {
				array_append(filename, linenum,
				    lookup_opcode_name(opcode),
				    chararrayptr, uintptr, arg2);
			}
			free(arg2);
		}
		break;

	case sForceCommand:
		if (cp == NULL || *cp == '\0')
			fatal("%.200s line %d: Missing argument.", filename,
			    linenum);
		len = strspn(cp, WHITESPACE);
		if (*activep && options->adm_forced_command == NULL)
			options->adm_forced_command = xstrdup(cp + len);
		return 0;

	case sChrootDirectory:
		charptr = &options->chroot_directory;

		arg = strdelim(&cp);
		if (!arg || *arg == '\0')
			fatal("%s line %d: missing file name.",
			    filename, linenum);
		if (*activep && *charptr == NULL)
			*charptr = xstrdup(arg);
		break;

	case sTrustedUserCAKeys:
		charptr = &options->trusted_user_ca_keys;
		goto parse_filename;

	case sRevokedKeys:
		charptr = &options->revoked_keys_file;
		goto parse_filename;

	case sIPQoS:
		arg = strdelim(&cp);
		if ((value = parse_ipqos(arg)) == -1)
			fatal("%s line %d: Bad IPQoS value: %s",
			    filename, linenum, arg);
		arg = strdelim(&cp);
		if (arg == NULL)
			value2 = value;
		else if ((value2 = parse_ipqos(arg)) == -1)
			fatal("%s line %d: Bad IPQoS value: %s",
			    filename, linenum, arg);
		if (*activep) {
			options->ip_qos_interactive = value;
			options->ip_qos_bulk = value2;
		}
		break;

	case sVersionAddendum:
		if (cp == NULL || *cp == '\0')
			fatal("%.200s line %d: Missing argument.", filename,
			    linenum);
		len = strspn(cp, WHITESPACE);
		if (*activep && options->version_addendum == NULL) {
			if (strcasecmp(cp + len, "none") == 0)
				options->version_addendum = xstrdup("");
			else if (strchr(cp + len, '\r') != NULL)
				fatal("%.200s line %d: Invalid argument",
				    filename, linenum);
			else
				options->version_addendum = xstrdup(cp + len);
		}
		return 0;

	case sAuthorizedKeysCommand:
		if (cp == NULL)
			fatal("%.200s line %d: Missing argument.", filename,
			    linenum);
		len = strspn(cp, WHITESPACE);
		if (*activep && options->authorized_keys_command == NULL) {
			if (cp[len] != '/' && strcasecmp(cp + len, "none") != 0)
				fatal("%.200s line %d: AuthorizedKeysCommand "
				    "must be an absolute path",
				    filename, linenum);
			options->authorized_keys_command = xstrdup(cp + len);
		}
		return 0;

	case sAuthorizedKeysCommandUser:
		charptr = &options->authorized_keys_command_user;

		arg = strdelim(&cp);
		if (!arg || *arg == '\0')
			fatal("%s line %d: missing AuthorizedKeysCommandUser "
			    "argument.", filename, linenum);
		if (*activep && *charptr == NULL)
			*charptr = xstrdup(arg);
		break;

	case sAuthorizedPrincipalsCommand:
		if (cp == NULL)
			fatal("%.200s line %d: Missing argument.", filename,
			    linenum);
		len = strspn(cp, WHITESPACE);
		if (*activep &&
		    options->authorized_principals_command == NULL) {
			if (cp[len] != '/' && strcasecmp(cp + len, "none") != 0)
				fatal("%.200s line %d: "
				    "AuthorizedPrincipalsCommand must be "
				    "an absolute path", filename, linenum);
			options->authorized_principals_command =
			    xstrdup(cp + len);
		}
		return 0;

	case sAuthorizedPrincipalsCommandUser:
		charptr = &options->authorized_principals_command_user;

		arg = strdelim(&cp);
		if (!arg || *arg == '\0')
			fatal("%s line %d: missing "
			    "AuthorizedPrincipalsCommandUser argument.",
			    filename, linenum);
		if (*activep && *charptr == NULL)
			*charptr = xstrdup(arg);
		break;

	case sAuthenticationMethods:
		if (options->num_auth_methods == 0) {
			value = 0; /* seen "any" pseudo-method */
			value2 = 0; /* successfully parsed any method */
			while ((arg = strdelim(&cp)) && *arg != '\0') {
				if (strcmp(arg, "any") == 0) {
					if (options->num_auth_methods > 0) {
						fatal("%s line %d: \"any\" "
						    "must appear alone in "
						    "AuthenticationMethods",
						    filename, linenum);
					}
					value = 1;
				} else if (value) {
					fatal("%s line %d: \"any\" must appear "
					    "alone in AuthenticationMethods",
					    filename, linenum);
				} else if (auth2_methods_valid(arg, 0) != 0) {
					fatal("%s line %d: invalid "
					    "authentication method list.",
					    filename, linenum);
				}
				value2 = 1;
				if (!*activep)
					continue;
				array_append(filename, linenum,
				    "AuthenticationMethods",
				    &options->auth_methods,
				    &options->num_auth_methods, arg);
			}
			if (value2 == 0) {
				fatal("%s line %d: no AuthenticationMethods "
				    "specified", filename, linenum);
			}
		}
		return 0;

	case sStreamLocalBindMask:
		arg = strdelim(&cp);
		if (!arg || *arg == '\0')
			fatal("%s line %d: missing StreamLocalBindMask "
			    "argument.", filename, linenum);
		/* Parse mode in octal format */
		value = strtol(arg, &p, 8);
		if (arg == p || value < 0 || value > 0777)
			fatal("%s line %d: Bad mask.", filename, linenum);
		if (*activep)
			options->fwd_opts.streamlocal_bind_mask = (mode_t)value;
		break;

	case sStreamLocalBindUnlink:
		intptr = &options->fwd_opts.streamlocal_bind_unlink;
		goto parse_flag;

	case sFingerprintHash:
		arg = strdelim(&cp);
		if (!arg || *arg == '\0')
			fatal("%.200s line %d: Missing argument.",
			    filename, linenum);
		if ((value = ssh_digest_alg_by_name(arg)) == -1)
			fatal("%.200s line %d: Invalid hash algorithm \"%s\".",
			    filename, linenum, arg);
		if (*activep)
			options->fingerprint_hash = value;
		break;

	case sExposeAuthInfo:
		intptr = &options->expose_userauth_info;
		goto parse_flag;

	case sRDomain:
		charptr = &options->routing_domain;
		arg = strdelim(&cp);
		if (!arg || *arg == '\0')
			fatal("%.200s line %d: Missing argument.",
			    filename, linenum);
		if (strcasecmp(arg, "none") != 0 && strcmp(arg, "%D") != 0 &&
		    !valid_rdomain(arg))
			fatal("%s line %d: bad routing domain",
			    filename, linenum);
		if (*activep && *charptr == NULL)
			*charptr = xstrdup(arg);
		break;

	case sUseBlacklist:
		intptr = &options->use_blacklist;
		goto parse_flag;

	case sDeprecated:
	case sIgnore:
	case sUnsupported:
		do_log2(opcode == sIgnore ?
		    SYSLOG_LEVEL_DEBUG2 : SYSLOG_LEVEL_INFO,
		    "%s line %d: %s option %s", filename, linenum,
		    opcode == sUnsupported ? "Unsupported" : "Deprecated", arg);
		while (arg)
		    arg = strdelim(&cp);
		break;

	default:
		fatal("%s line %d: Missing handler for opcode %s (%d)",
		    filename, linenum, arg, opcode);
	}
	if ((arg = strdelim(&cp)) != NULL && *arg != '\0')
		fatal("%s line %d: garbage at end of line; \"%.200s\".",
		    filename, linenum, arg);
	return 0;
}

/* Reads the server configuration file. */

void
load_server_config(const char *filename, struct sshbuf *conf)
{
	char *line = NULL, *cp;
	size_t linesize = 0;
	FILE *f;
	int r, lineno = 0;

	debug2("%s: filename %s", __func__, filename);
	if ((f = fopen(filename, "r")) == NULL) {
		perror(filename);
		exit(1);
	}
	sshbuf_reset(conf);
	while (getline(&line, &linesize, f) != -1) {
		lineno++;
		/*
		 * Trim out comments and strip whitespace
		 * NB - preserve newlines, they are needed to reproduce
		 * line numbers later for error messages
		 */
		if ((cp = strchr(line, '#')) != NULL)
			memcpy(cp, "\n", 2);
		cp = line + strspn(line, " \t\r");
		if ((r = sshbuf_put(conf, cp, strlen(cp))) != 0)
			fatal("%s: buffer error: %s", __func__, ssh_err(r));
	}
	free(line);
	if ((r = sshbuf_put_u8(conf, 0)) != 0)
		fatal("%s: buffer error: %s", __func__, ssh_err(r));
	fclose(f);
	debug2("%s: done config len = %zu", __func__, sshbuf_len(conf));
}

void
parse_server_match_config(ServerOptions *options,
   struct connection_info *connectinfo)
{
	ServerOptions mo;

	initialize_server_options(&mo);
	parse_server_config(&mo, "reprocess config", cfg, connectinfo);
	copy_set_server_options(options, &mo, 0);
}

int parse_server_match_testspec(struct connection_info *ci, char *spec)
{
	char *p;

	while ((p = strsep(&spec, ",")) && *p != '\0') {
		if (strncmp(p, "addr=", 5) == 0) {
			ci->address = xstrdup(p + 5);
		} else if (strncmp(p, "host=", 5) == 0) {
			ci->host = xstrdup(p + 5);
		} else if (strncmp(p, "user=", 5) == 0) {
			ci->user = xstrdup(p + 5);
		} else if (strncmp(p, "laddr=", 6) == 0) {
			ci->laddress = xstrdup(p + 6);
		} else if (strncmp(p, "rdomain=", 8) == 0) {
			ci->rdomain = xstrdup(p + 8);
		} else if (strncmp(p, "lport=", 6) == 0) {
			ci->lport = a2port(p + 6);
			if (ci->lport == -1) {
				fprintf(stderr, "Invalid port '%s' in test mode"
				   " specification %s\n", p+6, p);
				return -1;
			}
		} else {
			fprintf(stderr, "Invalid test mode specification %s\n",
			   p);
			return -1;
		}
	}
	return 0;
}

/*
 * Copy any supported values that are set.
 *
 * If the preauth flag is set, we do not bother copying the string or
 * array values that are not used pre-authentication, because any that we
 * do use must be explicitly sent in mm_getpwnamallow().
 */
void
copy_set_server_options(ServerOptions *dst, ServerOptions *src, int preauth)
{
#define M_CP_INTOPT(n) do {\
	if (src->n != -1) \
		dst->n = src->n; \
} while (0)

	M_CP_INTOPT(password_authentication);
	M_CP_INTOPT(gss_authentication);
	M_CP_INTOPT(pubkey_authentication);
	M_CP_INTOPT(kerberos_authentication);
	M_CP_INTOPT(hostbased_authentication);
	M_CP_INTOPT(hostbased_uses_name_from_packet_only);
	M_CP_INTOPT(kbd_interactive_authentication);
	M_CP_INTOPT(permit_root_login);
	M_CP_INTOPT(permit_empty_passwd);

	M_CP_INTOPT(allow_tcp_forwarding);
	M_CP_INTOPT(allow_streamlocal_forwarding);
	M_CP_INTOPT(allow_agent_forwarding);
	M_CP_INTOPT(disable_forwarding);
	M_CP_INTOPT(expose_userauth_info);
	M_CP_INTOPT(permit_tun);
	M_CP_INTOPT(fwd_opts.gateway_ports);
	M_CP_INTOPT(fwd_opts.streamlocal_bind_unlink);
	M_CP_INTOPT(x11_display_offset);
	M_CP_INTOPT(x11_forwarding);
	M_CP_INTOPT(x11_use_localhost);
	M_CP_INTOPT(permit_tty);
	M_CP_INTOPT(permit_user_rc);
	M_CP_INTOPT(max_sessions);
	M_CP_INTOPT(max_authtries);
	M_CP_INTOPT(client_alive_count_max);
	M_CP_INTOPT(client_alive_interval);
	M_CP_INTOPT(ip_qos_interactive);
	M_CP_INTOPT(ip_qos_bulk);
	M_CP_INTOPT(rekey_limit);
	M_CP_INTOPT(rekey_interval);
	M_CP_INTOPT(log_level);

	/*
	 * The bind_mask is a mode_t that may be unsigned, so we can't use
	 * M_CP_INTOPT - it does a signed comparison that causes compiler
	 * warnings.
	 */
	if (src->fwd_opts.streamlocal_bind_mask != (mode_t)-1) {
		dst->fwd_opts.streamlocal_bind_mask =
		    src->fwd_opts.streamlocal_bind_mask;
	}

	/* M_CP_STROPT and M_CP_STRARRAYOPT should not appear before here */
#define M_CP_STROPT(n) do {\
	if (src->n != NULL && dst->n != src->n) { \
		free(dst->n); \
		dst->n = src->n; \
	} \
} while(0)
#define M_CP_STRARRAYOPT(s, num_s) do {\
	u_int i; \
	if (src->num_s != 0) { \
		for (i = 0; i < dst->num_s; i++) \
			free(dst->s[i]); \
		free(dst->s); \
		dst->s = xcalloc(src->num_s, sizeof(*dst->s)); \
		for (i = 0; i < src->num_s; i++) \
			dst->s[i] = xstrdup(src->s[i]); \
		dst->num_s = src->num_s; \
	} \
} while(0)

	/* See comment in servconf.h */
	COPY_MATCH_STRING_OPTS();

	/* Arguments that accept '+...' need to be expanded */
	assemble_algorithms(dst);

	/*
	 * The only things that should be below this point are string options
	 * which are only used after authentication.
	 */
	if (preauth)
		return;

	/* These options may be "none" to clear a global setting */
	M_CP_STROPT(adm_forced_command);
	if (option_clear_or_none(dst->adm_forced_command)) {
		free(dst->adm_forced_command);
		dst->adm_forced_command = NULL;
	}
	M_CP_STROPT(chroot_directory);
	if (option_clear_or_none(dst->chroot_directory)) {
		free(dst->chroot_directory);
		dst->chroot_directory = NULL;
	}
}

#undef M_CP_INTOPT
#undef M_CP_STROPT
#undef M_CP_STRARRAYOPT

void
parse_server_config(ServerOptions *options, const char *filename,
    struct sshbuf *conf, struct connection_info *connectinfo)
{
	int active, linenum, bad_options = 0;
	char *cp, *obuf, *cbuf;

	debug2("%s: config %s len %zu", __func__, filename, sshbuf_len(conf));

	if ((obuf = cbuf = sshbuf_dup_string(conf)) == NULL)
		fatal("%s: sshbuf_dup_string failed", __func__);
	active = connectinfo ? 0 : 1;
	linenum = 1;
	while ((cp = strsep(&cbuf, "\n")) != NULL) {
		if (process_server_config_line(options, cp, filename,
		    linenum++, &active, connectinfo) != 0)
			bad_options++;
	}
	free(obuf);
	if (bad_options > 0)
		fatal("%s: terminating, %d bad configuration options",
		    filename, bad_options);
	process_queued_listen_addrs(options);
}

static const char *
fmt_multistate_int(int val, const struct multistate *m)
{
	u_int i;

	for (i = 0; m[i].key != NULL; i++) {
		if (m[i].value == val)
			return m[i].key;
	}
	return "UNKNOWN";
}

static const char *
fmt_intarg(ServerOpCodes code, int val)
{
	if (val == -1)
		return "unset";
	switch (code) {
	case sAddressFamily:
		return fmt_multistate_int(val, multistate_addressfamily);
	case sPermitRootLogin:
		return fmt_multistate_int(val, multistate_permitrootlogin);
	case sGatewayPorts:
		return fmt_multistate_int(val, multistate_gatewayports);
	case sCompression:
		return fmt_multistate_int(val, multistate_compression);
	case sAllowTcpForwarding:
		return fmt_multistate_int(val, multistate_tcpfwd);
	case sAllowStreamLocalForwarding:
		return fmt_multistate_int(val, multistate_tcpfwd);
	case sFingerprintHash:
		return ssh_digest_alg_name(val);
	default:
		switch (val) {
		case 0:
			return "no";
		case 1:
			return "yes";
		default:
			return "UNKNOWN";
		}
	}
}

static void
dump_cfg_int(ServerOpCodes code, int val)
{
	printf("%s %d\n", lookup_opcode_name(code), val);
}

static void
dump_cfg_oct(ServerOpCodes code, int val)
{
	printf("%s 0%o\n", lookup_opcode_name(code), val);
}

static void
dump_cfg_fmtint(ServerOpCodes code, int val)
{
	printf("%s %s\n", lookup_opcode_name(code), fmt_intarg(code, val));
}

static void
dump_cfg_string(ServerOpCodes code, const char *val)
{
	printf("%s %s\n", lookup_opcode_name(code),
	    val == NULL ? "none" : val);
}

static void
dump_cfg_strarray(ServerOpCodes code, u_int count, char **vals)
{
	u_int i;

	for (i = 0; i < count; i++)
		printf("%s %s\n", lookup_opcode_name(code), vals[i]);
}

static void
dump_cfg_strarray_oneline(ServerOpCodes code, u_int count, char **vals)
{
	u_int i;

	if (count <= 0 && code != sAuthenticationMethods)
		return;
	printf("%s", lookup_opcode_name(code));
	for (i = 0; i < count; i++)
		printf(" %s",  vals[i]);
	if (code == sAuthenticationMethods && count == 0)
		printf(" any");
	printf("\n");
}

static char *
format_listen_addrs(struct listenaddr *la)
{
	int r;
	struct addrinfo *ai;
	char addr[NI_MAXHOST], port[NI_MAXSERV];
	char *laddr1 = xstrdup(""), *laddr2 = NULL;

	/*
	 * ListenAddress must be after Port.  add_one_listen_addr pushes
	 * addresses onto a stack, so to maintain ordering we need to
	 * print these in reverse order.
	 */
	for (ai = la->addrs; ai; ai = ai->ai_next) {
		if ((r = getnameinfo(ai->ai_addr, ai->ai_addrlen, addr,
		    sizeof(addr), port, sizeof(port),
		    NI_NUMERICHOST|NI_NUMERICSERV)) != 0) {
			error("getnameinfo: %.100s", ssh_gai_strerror(r));
			continue;
		}
		laddr2 = laddr1;
		if (ai->ai_family == AF_INET6) {
			xasprintf(&laddr1, "listenaddress [%s]:%s%s%s\n%s",
			    addr, port,
			    la->rdomain == NULL ? "" : " rdomain ",
			    la->rdomain == NULL ? "" : la->rdomain,
			    laddr2);
		} else {
			xasprintf(&laddr1, "listenaddress %s:%s%s%s\n%s",
			    addr, port,
			    la->rdomain == NULL ? "" : " rdomain ",
			    la->rdomain == NULL ? "" : la->rdomain,
			    laddr2);
		}
		free(laddr2);
	}
	return laddr1;
}

void
dump_config(ServerOptions *o)
{
	char *s;
	u_int i;

	/* these are usually at the top of the config */
	for (i = 0; i < o->num_ports; i++)
		printf("port %d\n", o->ports[i]);
	dump_cfg_fmtint(sAddressFamily, o->address_family);

	for (i = 0; i < o->num_listen_addrs; i++) {
		s = format_listen_addrs(&o->listen_addrs[i]);
		printf("%s", s);
		free(s);
	}

	/* integer arguments */
#ifdef USE_PAM
	dump_cfg_fmtint(sUsePAM, o->use_pam);
#endif
	dump_cfg_int(sLoginGraceTime, o->login_grace_time);
	dump_cfg_int(sX11DisplayOffset, o->x11_display_offset);
	dump_cfg_int(sMaxAuthTries, o->max_authtries);
	dump_cfg_int(sMaxSessions, o->max_sessions);
	dump_cfg_int(sClientAliveInterval, o->client_alive_interval);
	dump_cfg_int(sClientAliveCountMax, o->client_alive_count_max);
	dump_cfg_oct(sStreamLocalBindMask, o->fwd_opts.streamlocal_bind_mask);

	/* formatted integer arguments */
	dump_cfg_fmtint(sPermitRootLogin, o->permit_root_login);
	dump_cfg_fmtint(sIgnoreRhosts, o->ignore_rhosts);
	dump_cfg_fmtint(sIgnoreUserKnownHosts, o->ignore_user_known_hosts);
	dump_cfg_fmtint(sHostbasedAuthentication, o->hostbased_authentication);
	dump_cfg_fmtint(sHostbasedUsesNameFromPacketOnly,
	    o->hostbased_uses_name_from_packet_only);
	dump_cfg_fmtint(sPubkeyAuthentication, o->pubkey_authentication);
#ifdef KRB5
	dump_cfg_fmtint(sKerberosAuthentication, o->kerberos_authentication);
	dump_cfg_fmtint(sKerberosOrLocalPasswd, o->kerberos_or_local_passwd);
	dump_cfg_fmtint(sKerberosTicketCleanup, o->kerberos_ticket_cleanup);
# ifdef USE_AFS
	dump_cfg_fmtint(sKerberosGetAFSToken, o->kerberos_get_afs_token);
# endif
#endif
#ifdef GSSAPI
	dump_cfg_fmtint(sGssAuthentication, o->gss_authentication);
	dump_cfg_fmtint(sGssCleanupCreds, o->gss_cleanup_creds);
#endif
	dump_cfg_fmtint(sPasswordAuthentication, o->password_authentication);
	dump_cfg_fmtint(sKbdInteractiveAuthentication,
	    o->kbd_interactive_authentication);
	dump_cfg_fmtint(sChallengeResponseAuthentication,
	    o->challenge_response_authentication);
	dump_cfg_fmtint(sPrintMotd, o->print_motd);
#ifndef DISABLE_LASTLOG
	dump_cfg_fmtint(sPrintLastLog, o->print_lastlog);
#endif
	dump_cfg_fmtint(sX11Forwarding, o->x11_forwarding);
	dump_cfg_fmtint(sX11UseLocalhost, o->x11_use_localhost);
	dump_cfg_fmtint(sPermitTTY, o->permit_tty);
	dump_cfg_fmtint(sPermitUserRC, o->permit_user_rc);
	dump_cfg_fmtint(sStrictModes, o->strict_modes);
	dump_cfg_fmtint(sTCPKeepAlive, o->tcp_keep_alive);
	dump_cfg_fmtint(sEmptyPasswd, o->permit_empty_passwd);
	dump_cfg_fmtint(sCompression, o->compression);
	dump_cfg_fmtint(sGatewayPorts, o->fwd_opts.gateway_ports);
	dump_cfg_fmtint(sUseDNS, o->use_dns);
	dump_cfg_fmtint(sAllowTcpForwarding, o->allow_tcp_forwarding);
	dump_cfg_fmtint(sAllowAgentForwarding, o->allow_agent_forwarding);
	dump_cfg_fmtint(sDisableForwarding, o->disable_forwarding);
	dump_cfg_fmtint(sAllowStreamLocalForwarding, o->allow_streamlocal_forwarding);
	dump_cfg_fmtint(sStreamLocalBindUnlink, o->fwd_opts.streamlocal_bind_unlink);
	dump_cfg_fmtint(sFingerprintHash, o->fingerprint_hash);
	dump_cfg_fmtint(sExposeAuthInfo, o->expose_userauth_info);
	dump_cfg_fmtint(sUseBlacklist, o->use_blacklist);

	/* string arguments */
	dump_cfg_string(sPidFile, o->pid_file);
	dump_cfg_string(sXAuthLocation, o->xauth_location);
	dump_cfg_string(sCiphers, o->ciphers ? o->ciphers : KEX_SERVER_ENCRYPT);
	dump_cfg_string(sMacs, o->macs ? o->macs : KEX_SERVER_MAC);
	dump_cfg_string(sBanner, o->banner);
	dump_cfg_string(sForceCommand, o->adm_forced_command);
	dump_cfg_string(sChrootDirectory, o->chroot_directory);
	dump_cfg_string(sTrustedUserCAKeys, o->trusted_user_ca_keys);
	dump_cfg_string(sRevokedKeys, o->revoked_keys_file);
	dump_cfg_string(sAuthorizedPrincipalsFile,
	    o->authorized_principals_file);
	dump_cfg_string(sVersionAddendum, *o->version_addendum == '\0'
	    ? "none" : o->version_addendum);
	dump_cfg_string(sAuthorizedKeysCommand, o->authorized_keys_command);
	dump_cfg_string(sAuthorizedKeysCommandUser, o->authorized_keys_command_user);
	dump_cfg_string(sAuthorizedPrincipalsCommand, o->authorized_principals_command);
	dump_cfg_string(sAuthorizedPrincipalsCommandUser, o->authorized_principals_command_user);
	dump_cfg_string(sHostKeyAgent, o->host_key_agent);
	dump_cfg_string(sKexAlgorithms,
	    o->kex_algorithms ? o->kex_algorithms : KEX_SERVER_KEX);
	dump_cfg_string(sHostbasedAcceptedKeyTypes, o->hostbased_key_types ?
	    o->hostbased_key_types : KEX_DEFAULT_PK_ALG);
	dump_cfg_string(sHostKeyAlgorithms, o->hostkeyalgorithms ?
	    o->hostkeyalgorithms : KEX_DEFAULT_PK_ALG);
	dump_cfg_string(sPubkeyAcceptedKeyTypes, o->pubkey_key_types ?
	    o->pubkey_key_types : KEX_DEFAULT_PK_ALG);
	dump_cfg_string(sRDomain, o->routing_domain);

	/* string arguments requiring a lookup */
	dump_cfg_string(sLogLevel, log_level_name(o->log_level));
	dump_cfg_string(sLogFacility, log_facility_name(o->log_facility));

	/* string array arguments */
	dump_cfg_strarray_oneline(sAuthorizedKeysFile, o->num_authkeys_files,
	    o->authorized_keys_files);
	dump_cfg_strarray(sHostKeyFile, o->num_host_key_files,
	     o->host_key_files);
	dump_cfg_strarray(sHostCertificate, o->num_host_cert_files,
	     o->host_cert_files);
	dump_cfg_strarray(sAllowUsers, o->num_allow_users, o->allow_users);
	dump_cfg_strarray(sDenyUsers, o->num_deny_users, o->deny_users);
	dump_cfg_strarray(sAllowGroups, o->num_allow_groups, o->allow_groups);
	dump_cfg_strarray(sDenyGroups, o->num_deny_groups, o->deny_groups);
	dump_cfg_strarray(sAcceptEnv, o->num_accept_env, o->accept_env);
	dump_cfg_strarray(sSetEnv, o->num_setenv, o->setenv);
	dump_cfg_strarray_oneline(sAuthenticationMethods,
	    o->num_auth_methods, o->auth_methods);

	/* other arguments */
	for (i = 0; i < o->num_subsystems; i++)
		printf("subsystem %s %s\n", o->subsystem_name[i],
		    o->subsystem_args[i]);

	printf("maxstartups %d:%d:%d\n", o->max_startups_begin,
	    o->max_startups_rate, o->max_startups);

	s = NULL;
	for (i = 0; tunmode_desc[i].val != -1; i++) {
		if (tunmode_desc[i].val == o->permit_tun) {
			s = tunmode_desc[i].text;
			break;
		}
	}
	dump_cfg_string(sPermitTunnel, s);

	printf("ipqos %s ", iptos2str(o->ip_qos_interactive));
	printf("%s\n", iptos2str(o->ip_qos_bulk));

	printf("rekeylimit %llu %d\n", (unsigned long long)o->rekey_limit,
	    o->rekey_interval);

	printf("permitopen");
	if (o->num_permitted_opens == 0)
		printf(" any");
	else {
		for (i = 0; i < o->num_permitted_opens; i++)
			printf(" %s", o->permitted_opens[i]);
	}
	printf("\n");
	printf("permitlisten");
	if (o->num_permitted_listens == 0)
		printf(" any");
	else {
		for (i = 0; i < o->num_permitted_listens; i++)
			printf(" %s", o->permitted_listens[i]);
	}
	printf("\n");

	if (o->permit_user_env_whitelist == NULL) {
		dump_cfg_fmtint(sPermitUserEnvironment, o->permit_user_env);
	} else {
		printf("permituserenvironment %s\n",
		    o->permit_user_env_whitelist);
	}

}
