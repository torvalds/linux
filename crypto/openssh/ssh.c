/* $OpenBSD: ssh.c,v 1.490 2018/07/27 05:34:42 dtucker Exp $ */
/*
 * Author: Tatu Ylonen <ylo@cs.hut.fi>
 * Copyright (c) 1995 Tatu Ylonen <ylo@cs.hut.fi>, Espoo, Finland
 *                    All rights reserved
 * Ssh client program.  This program can be used to log into a remote machine.
 * The software supports strong authentication, encryption, and forwarding
 * of X11, TCP/IP, and authentication connections.
 *
 * As far as I am concerned, the code I have written for this software
 * can be used freely for any purpose.  Any derived versions of this
 * software must be clearly marked as such, and if the derived work is
 * incompatible with the protocol description in the RFC file, it must be
 * called by a name other than "ssh" or "Secure Shell".
 *
 * Copyright (c) 1999 Niels Provos.  All rights reserved.
 * Copyright (c) 2000, 2001, 2002, 2003 Markus Friedl.  All rights reserved.
 *
 * Modified to work with SSL by Niels Provos <provos@citi.umich.edu>
 * in Canada (German citizen).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "includes.h"
__RCSID("$FreeBSD$");

#include <sys/types.h>
#ifdef HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif
#include <sys/resource.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#ifdef HAVE_PATHS_H
#include <paths.h>
#endif
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <locale.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#ifdef WITH_OPENSSL
#include <openssl/evp.h>
#include <openssl/err.h>
#endif
#include "openbsd-compat/openssl-compat.h"
#include "openbsd-compat/sys-queue.h"

#include "xmalloc.h"
#include "ssh.h"
#include "ssh2.h"
#include "canohost.h"
#include "compat.h"
#include "cipher.h"
#include "digest.h"
#include "packet.h"
#include "sshbuf.h"
#include "channels.h"
#include "sshkey.h"
#include "authfd.h"
#include "authfile.h"
#include "pathnames.h"
#include "dispatch.h"
#include "clientloop.h"
#include "log.h"
#include "misc.h"
#include "readconf.h"
#include "sshconnect.h"
#include "kex.h"
#include "mac.h"
#include "sshpty.h"
#include "match.h"
#include "msg.h"
#include "version.h"
#include "ssherr.h"
#include "myproposal.h"
#include "utf8.h"

#ifdef ENABLE_PKCS11
#include "ssh-pkcs11.h"
#endif

extern char *__progname;

/* Saves a copy of argv for setproctitle emulation */
#ifndef HAVE_SETPROCTITLE
static char **saved_av;
#endif

/* Flag indicating whether debug mode is on.  May be set on the command line. */
int debug_flag = 0;

/* Flag indicating whether a tty should be requested */
int tty_flag = 0;

/* don't exec a shell */
int no_shell_flag = 0;

/*
 * Flag indicating that nothing should be read from stdin.  This can be set
 * on the command line.
 */
int stdin_null_flag = 0;

/*
 * Flag indicating that the current process should be backgrounded and
 * a new slave launched in the foreground for ControlPersist.
 */
int need_controlpersist_detach = 0;

/* Copies of flags for ControlPersist foreground slave */
int ostdin_null_flag, ono_shell_flag, otty_flag, orequest_tty;

/*
 * Flag indicating that ssh should fork after authentication.  This is useful
 * so that the passphrase can be entered manually, and then ssh goes to the
 * background.
 */
int fork_after_authentication_flag = 0;

/*
 * General data structure for command line options and options configurable
 * in configuration files.  See readconf.h.
 */
Options options;

/* optional user configfile */
char *config = NULL;

/*
 * Name of the host we are connecting to.  This is the name given on the
 * command line, or the HostName specified for the user-supplied name in a
 * configuration file.
 */
char *host;

/* Various strings used to to percent_expand() arguments */
static char thishost[NI_MAXHOST], shorthost[NI_MAXHOST], portstr[NI_MAXSERV];
static char uidstr[32], *host_arg, *conn_hash_hex;

/* socket address the host resolves to */
struct sockaddr_storage hostaddr;

/* Private host keys. */
Sensitive sensitive_data;

/* command to be executed */
struct sshbuf *command;

/* Should we execute a command or invoke a subsystem? */
int subsystem_flag = 0;

/* # of replies received for global requests */
static int remote_forward_confirms_received = 0;

/* mux.c */
extern int muxserver_sock;
extern u_int muxclient_command;

/* Prints a help message to the user.  This function never returns. */

static void
usage(void)
{
	fprintf(stderr,
"usage: ssh [-46AaCfGgKkMNnqsTtVvXxYy] [-B bind_interface]\n"
"           [-b bind_address] [-c cipher_spec] [-D [bind_address:]port]\n"
"           [-E log_file] [-e escape_char] [-F configfile] [-I pkcs11]\n"
"           [-i identity_file] [-J [user@]host[:port]] [-L address]\n"
"           [-l login_name] [-m mac_spec] [-O ctl_cmd] [-o option] [-p port]\n"
"           [-Q query_option] [-R address] [-S ctl_path] [-W host:port]\n"
"           [-w local_tun[:remote_tun]] destination [command]\n"
	);
	exit(255);
}

static int ssh_session2(struct ssh *, struct passwd *);
static void load_public_identity_files(struct passwd *);
static void main_sigchld_handler(int);

/* ~/ expand a list of paths. NB. assumes path[n] is heap-allocated. */
static void
tilde_expand_paths(char **paths, u_int num_paths)
{
	u_int i;
	char *cp;

	for (i = 0; i < num_paths; i++) {
		cp = tilde_expand_filename(paths[i], getuid());
		free(paths[i]);
		paths[i] = cp;
	}
}

/*
 * Attempt to resolve a host name / port to a set of addresses and
 * optionally return any CNAMEs encountered along the way.
 * Returns NULL on failure.
 * NB. this function must operate with a options having undefined members.
 */
static struct addrinfo *
resolve_host(const char *name, int port, int logerr, char *cname, size_t clen)
{
	char strport[NI_MAXSERV];
	struct addrinfo hints, *res;
	int gaierr, loglevel = SYSLOG_LEVEL_DEBUG1;

	if (port <= 0)
		port = default_ssh_port();

	snprintf(strport, sizeof strport, "%d", port);
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = options.address_family == -1 ?
	    AF_UNSPEC : options.address_family;
	hints.ai_socktype = SOCK_STREAM;
	if (cname != NULL)
		hints.ai_flags = AI_CANONNAME;
	if ((gaierr = getaddrinfo(name, strport, &hints, &res)) != 0) {
		if (logerr || (gaierr != EAI_NONAME && gaierr != EAI_NODATA))
			loglevel = SYSLOG_LEVEL_ERROR;
		do_log2(loglevel, "%s: Could not resolve hostname %.100s: %s",
		    __progname, name, ssh_gai_strerror(gaierr));
		return NULL;
	}
	if (cname != NULL && res->ai_canonname != NULL) {
		if (strlcpy(cname, res->ai_canonname, clen) >= clen) {
			error("%s: host \"%s\" cname \"%s\" too long (max %lu)",
			    __func__, name,  res->ai_canonname, (u_long)clen);
			if (clen > 0)
				*cname = '\0';
		}
	}
	return res;
}

/* Returns non-zero if name can only be an address and not a hostname */
static int
is_addr_fast(const char *name)
{
	return (strchr(name, '%') != NULL || strchr(name, ':') != NULL ||
	    strspn(name, "0123456789.") == strlen(name));
}

/* Returns non-zero if name represents a valid, single address */
static int
is_addr(const char *name)
{
	char strport[NI_MAXSERV];
	struct addrinfo hints, *res;

	if (is_addr_fast(name))
		return 1;

	snprintf(strport, sizeof strport, "%u", default_ssh_port());
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = options.address_family == -1 ?
	    AF_UNSPEC : options.address_family;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_NUMERICHOST|AI_NUMERICSERV;
	if (getaddrinfo(name, strport, &hints, &res) != 0)
		return 0;
	if (res == NULL || res->ai_next != NULL) {
		freeaddrinfo(res);
		return 0;
	}
	freeaddrinfo(res);
	return 1;
}

/*
 * Attempt to resolve a numeric host address / port to a single address.
 * Returns a canonical address string.
 * Returns NULL on failure.
 * NB. this function must operate with a options having undefined members.
 */
static struct addrinfo *
resolve_addr(const char *name, int port, char *caddr, size_t clen)
{
	char addr[NI_MAXHOST], strport[NI_MAXSERV];
	struct addrinfo hints, *res;
	int gaierr;

	if (port <= 0)
		port = default_ssh_port();
	snprintf(strport, sizeof strport, "%u", port);
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = options.address_family == -1 ?
	    AF_UNSPEC : options.address_family;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_NUMERICHOST|AI_NUMERICSERV;
	if ((gaierr = getaddrinfo(name, strport, &hints, &res)) != 0) {
		debug2("%s: could not resolve name %.100s as address: %s",
		    __func__, name, ssh_gai_strerror(gaierr));
		return NULL;
	}
	if (res == NULL) {
		debug("%s: getaddrinfo %.100s returned no addresses",
		 __func__, name);
		return NULL;
	}
	if (res->ai_next != NULL) {
		debug("%s: getaddrinfo %.100s returned multiple addresses",
		    __func__, name);
		goto fail;
	}
	if ((gaierr = getnameinfo(res->ai_addr, res->ai_addrlen,
	    addr, sizeof(addr), NULL, 0, NI_NUMERICHOST)) != 0) {
		debug("%s: Could not format address for name %.100s: %s",
		    __func__, name, ssh_gai_strerror(gaierr));
		goto fail;
	}
	if (strlcpy(caddr, addr, clen) >= clen) {
		error("%s: host \"%s\" addr \"%s\" too long (max %lu)",
		    __func__, name,  addr, (u_long)clen);
		if (clen > 0)
			*caddr = '\0';
 fail:
		freeaddrinfo(res);
		return NULL;
	}
	return res;
}

/*
 * Check whether the cname is a permitted replacement for the hostname
 * and perform the replacement if it is.
 * NB. this function must operate with a options having undefined members.
 */
static int
check_follow_cname(int direct, char **namep, const char *cname)
{
	int i;
	struct allowed_cname *rule;

	if (*cname == '\0' || options.num_permitted_cnames == 0 ||
	    strcmp(*namep, cname) == 0)
		return 0;
	if (options.canonicalize_hostname == SSH_CANONICALISE_NO)
		return 0;
	/*
	 * Don't attempt to canonicalize names that will be interpreted by
	 * a proxy or jump host unless the user specifically requests so.
	 */
	if (!direct &&
	    options.canonicalize_hostname != SSH_CANONICALISE_ALWAYS)
		return 0;
	debug3("%s: check \"%s\" CNAME \"%s\"", __func__, *namep, cname);
	for (i = 0; i < options.num_permitted_cnames; i++) {
		rule = options.permitted_cnames + i;
		if (match_pattern_list(*namep, rule->source_list, 1) != 1 ||
		    match_pattern_list(cname, rule->target_list, 1) != 1)
			continue;
		verbose("Canonicalized DNS aliased hostname "
		    "\"%s\" => \"%s\"", *namep, cname);
		free(*namep);
		*namep = xstrdup(cname);
		return 1;
	}
	return 0;
}

/*
 * Attempt to resolve the supplied hostname after applying the user's
 * canonicalization rules. Returns the address list for the host or NULL
 * if no name was found after canonicalization.
 * NB. this function must operate with a options having undefined members.
 */
static struct addrinfo *
resolve_canonicalize(char **hostp, int port)
{
	int i, direct, ndots;
	char *cp, *fullhost, newname[NI_MAXHOST];
	struct addrinfo *addrs;

	/*
	 * Attempt to canonicalise addresses, regardless of
	 * whether hostname canonicalisation was requested
	 */
	if ((addrs = resolve_addr(*hostp, port,
	    newname, sizeof(newname))) != NULL) {
		debug2("%s: hostname %.100s is address", __func__, *hostp);
		if (strcasecmp(*hostp, newname) != 0) {
			debug2("%s: canonicalised address \"%s\" => \"%s\"",
			    __func__, *hostp, newname);
			free(*hostp);
			*hostp = xstrdup(newname);
		}
		return addrs;
	}

	/*
	 * If this looks like an address but didn't parse as one, it might
	 * be an address with an invalid interface scope. Skip further
	 * attempts at canonicalisation.
	 */
	if (is_addr_fast(*hostp)) {
		debug("%s: hostname %.100s is an unrecognised address",
		    __func__, *hostp);
		return NULL;
	}

	if (options.canonicalize_hostname == SSH_CANONICALISE_NO)
		return NULL;

	/*
	 * Don't attempt to canonicalize names that will be interpreted by
	 * a proxy unless the user specifically requests so.
	 */
	direct = option_clear_or_none(options.proxy_command) &&
	    options.jump_host == NULL;
	if (!direct &&
	    options.canonicalize_hostname != SSH_CANONICALISE_ALWAYS)
		return NULL;

	/* If domain name is anchored, then resolve it now */
	if ((*hostp)[strlen(*hostp) - 1] == '.') {
		debug3("%s: name is fully qualified", __func__);
		fullhost = xstrdup(*hostp);
		if ((addrs = resolve_host(fullhost, port, 0,
		    newname, sizeof(newname))) != NULL)
			goto found;
		free(fullhost);
		goto notfound;
	}

	/* Don't apply canonicalization to sufficiently-qualified hostnames */
	ndots = 0;
	for (cp = *hostp; *cp != '\0'; cp++) {
		if (*cp == '.')
			ndots++;
	}
	if (ndots > options.canonicalize_max_dots) {
		debug3("%s: not canonicalizing hostname \"%s\" (max dots %d)",
		    __func__, *hostp, options.canonicalize_max_dots);
		return NULL;
	}
	/* Attempt each supplied suffix */
	for (i = 0; i < options.num_canonical_domains; i++) {
		*newname = '\0';
		xasprintf(&fullhost, "%s.%s.", *hostp,
		    options.canonical_domains[i]);
		debug3("%s: attempting \"%s\" => \"%s\"", __func__,
		    *hostp, fullhost);
		if ((addrs = resolve_host(fullhost, port, 0,
		    newname, sizeof(newname))) == NULL) {
			free(fullhost);
			continue;
		}
 found:
		/* Remove trailing '.' */
		fullhost[strlen(fullhost) - 1] = '\0';
		/* Follow CNAME if requested */
		if (!check_follow_cname(direct, &fullhost, newname)) {
			debug("Canonicalized hostname \"%s\" => \"%s\"",
			    *hostp, fullhost);
		}
		free(*hostp);
		*hostp = fullhost;
		return addrs;
	}
 notfound:
	if (!options.canonicalize_fallback_local)
		fatal("%s: Could not resolve host \"%s\"", __progname, *hostp);
	debug2("%s: host %s not found in any suffix", __func__, *hostp);
	return NULL;
}

/*
 * Check the result of hostkey loading, ignoring some errors and
 * fatal()ing for others.
 */
static void
check_load(int r, const char *path, const char *message)
{
	switch (r) {
	case 0:
		break;
	case SSH_ERR_INTERNAL_ERROR:
	case SSH_ERR_ALLOC_FAIL:
		fatal("load %s \"%s\": %s", message, path, ssh_err(r));
	case SSH_ERR_SYSTEM_ERROR:
		/* Ignore missing files */
		if (errno == ENOENT)
			break;
		/* FALLTHROUGH */
	default:
		error("load %s \"%s\": %s", message, path, ssh_err(r));
		break;
	}
}

/*
 * Read per-user configuration file.  Ignore the system wide config
 * file if the user specifies a config file on the command line.
 */
static void
process_config_files(const char *host_name, struct passwd *pw, int post_canon)
{
	char buf[PATH_MAX];
	int r;

	if (config != NULL) {
		if (strcasecmp(config, "none") != 0 &&
		    !read_config_file(config, pw, host, host_name, &options,
		    SSHCONF_USERCONF | (post_canon ? SSHCONF_POSTCANON : 0)))
			fatal("Can't open user config file %.100s: "
			    "%.100s", config, strerror(errno));
	} else {
		r = snprintf(buf, sizeof buf, "%s/%s", pw->pw_dir,
		    _PATH_SSH_USER_CONFFILE);
		if (r > 0 && (size_t)r < sizeof(buf))
			(void)read_config_file(buf, pw, host, host_name,
			    &options, SSHCONF_CHECKPERM | SSHCONF_USERCONF |
			    (post_canon ? SSHCONF_POSTCANON : 0));

		/* Read systemwide configuration file after user config. */
		(void)read_config_file(_PATH_HOST_CONFIG_FILE, pw,
		    host, host_name, &options,
		    post_canon ? SSHCONF_POSTCANON : 0);
	}
}

/* Rewrite the port number in an addrinfo list of addresses */
static void
set_addrinfo_port(struct addrinfo *addrs, int port)
{
	struct addrinfo *addr;

	for (addr = addrs; addr != NULL; addr = addr->ai_next) {
		switch (addr->ai_family) {
		case AF_INET:
			((struct sockaddr_in *)addr->ai_addr)->
			    sin_port = htons(port);
			break;
		case AF_INET6:
			((struct sockaddr_in6 *)addr->ai_addr)->
			    sin6_port = htons(port);
			break;
		}
	}
}

/*
 * Main program for the ssh client.
 */
int
main(int ac, char **av)
{
	struct ssh *ssh = NULL;
	int i, r, opt, exit_status, use_syslog, direct, timeout_ms;
	int was_addr, config_test = 0, opt_terminated = 0;
	char *p, *cp, *line, *argv0, buf[PATH_MAX], *logfile;
	char cname[NI_MAXHOST];
	struct stat st;
	struct passwd *pw;
	extern int optind, optreset;
	extern char *optarg;
	struct Forward fwd;
	struct addrinfo *addrs = NULL;
	struct ssh_digest_ctx *md;
	u_char conn_hash[SSH_DIGEST_MAX_LENGTH];

	ssh_malloc_init();	/* must be called before any mallocs */
	/* Ensure that fds 0, 1 and 2 are open or directed to /dev/null */
	sanitise_stdfd();

	__progname = ssh_get_progname(av[0]);

#ifndef HAVE_SETPROCTITLE
	/* Prepare for later setproctitle emulation */
	/* Save argv so it isn't clobbered by setproctitle() emulation */
	saved_av = xcalloc(ac + 1, sizeof(*saved_av));
	for (i = 0; i < ac; i++)
		saved_av[i] = xstrdup(av[i]);
	saved_av[i] = NULL;
	compat_init_setproctitle(ac, av);
	av = saved_av;
#endif

	/*
	 * Discard other fds that are hanging around. These can cause problem
	 * with backgrounded ssh processes started by ControlPersist.
	 */
	closefrom(STDERR_FILENO + 1);

	/* Get user data. */
	pw = getpwuid(getuid());
	if (!pw) {
		logit("No user exists for uid %lu", (u_long)getuid());
		exit(255);
	}
	/* Take a copy of the returned structure. */
	pw = pwcopy(pw);

	/*
	 * Set our umask to something reasonable, as some files are created
	 * with the default umask.  This will make them world-readable but
	 * writable only by the owner, which is ok for all files for which we
	 * don't set the modes explicitly.
	 */
	umask(022);

	msetlocale();

	/*
	 * Initialize option structure to indicate that no values have been
	 * set.
	 */
	initialize_options(&options);

	/*
	 * Prepare main ssh transport/connection structures
	 */
	if ((ssh = ssh_alloc_session_state()) == NULL)
		fatal("Couldn't allocate session state");
	channel_init_channels(ssh);
	active_state = ssh; /* XXX legacy API compat */

	/* Parse command-line arguments. */
	host = NULL;
	use_syslog = 0;
	logfile = NULL;
	argv0 = av[0];

 again:
	while ((opt = getopt(ac, av, "1246ab:c:e:fgi:kl:m:no:p:qstvx"
	    "AB:CD:E:F:GI:J:KL:MNO:PQ:R:S:TVw:W:XYy")) != -1) {
		switch (opt) {
		case '1':
			fatal("SSH protocol v.1 is no longer supported");
			break;
		case '2':
			/* Ignored */
			break;
		case '4':
			options.address_family = AF_INET;
			break;
		case '6':
			options.address_family = AF_INET6;
			break;
		case 'n':
			stdin_null_flag = 1;
			break;
		case 'f':
			fork_after_authentication_flag = 1;
			stdin_null_flag = 1;
			break;
		case 'x':
			options.forward_x11 = 0;
			break;
		case 'X':
			options.forward_x11 = 1;
			break;
		case 'y':
			use_syslog = 1;
			break;
		case 'E':
			logfile = optarg;
			break;
		case 'G':
			config_test = 1;
			break;
		case 'Y':
			options.forward_x11 = 1;
			options.forward_x11_trusted = 1;
			break;
		case 'g':
			options.fwd_opts.gateway_ports = 1;
			break;
		case 'O':
			if (options.stdio_forward_host != NULL)
				fatal("Cannot specify multiplexing "
				    "command with -W");
			else if (muxclient_command != 0)
				fatal("Multiplexing command already specified");
			if (strcmp(optarg, "check") == 0)
				muxclient_command = SSHMUX_COMMAND_ALIVE_CHECK;
			else if (strcmp(optarg, "forward") == 0)
				muxclient_command = SSHMUX_COMMAND_FORWARD;
			else if (strcmp(optarg, "exit") == 0)
				muxclient_command = SSHMUX_COMMAND_TERMINATE;
			else if (strcmp(optarg, "stop") == 0)
				muxclient_command = SSHMUX_COMMAND_STOP;
			else if (strcmp(optarg, "cancel") == 0)
				muxclient_command = SSHMUX_COMMAND_CANCEL_FWD;
			else if (strcmp(optarg, "proxy") == 0)
				muxclient_command = SSHMUX_COMMAND_PROXY;
			else
				fatal("Invalid multiplex command.");
			break;
		case 'P':	/* deprecated */
			break;
		case 'Q':
			cp = NULL;
			if (strcmp(optarg, "cipher") == 0)
				cp = cipher_alg_list('\n', 0);
			else if (strcmp(optarg, "cipher-auth") == 0)
				cp = cipher_alg_list('\n', 1);
			else if (strcmp(optarg, "mac") == 0)
				cp = mac_alg_list('\n');
			else if (strcmp(optarg, "kex") == 0)
				cp = kex_alg_list('\n');
			else if (strcmp(optarg, "key") == 0)
				cp = sshkey_alg_list(0, 0, 0, '\n');
			else if (strcmp(optarg, "key-cert") == 0)
				cp = sshkey_alg_list(1, 0, 0, '\n');
			else if (strcmp(optarg, "key-plain") == 0)
				cp = sshkey_alg_list(0, 1, 0, '\n');
			else if (strcmp(optarg, "protocol-version") == 0) {
				cp = xstrdup("2");
			}
			if (cp == NULL)
				fatal("Unsupported query \"%s\"", optarg);
			printf("%s\n", cp);
			free(cp);
			exit(0);
			break;
		case 'a':
			options.forward_agent = 0;
			break;
		case 'A':
			options.forward_agent = 1;
			break;
		case 'k':
			options.gss_deleg_creds = 0;
			break;
		case 'K':
			options.gss_authentication = 1;
			options.gss_deleg_creds = 1;
			break;
		case 'i':
			p = tilde_expand_filename(optarg, getuid());
			if (stat(p, &st) < 0)
				fprintf(stderr, "Warning: Identity file %s "
				    "not accessible: %s.\n", p,
				    strerror(errno));
			else
				add_identity_file(&options, NULL, p, 1);
			free(p);
			break;
		case 'I':
#ifdef ENABLE_PKCS11
			free(options.pkcs11_provider);
			options.pkcs11_provider = xstrdup(optarg);
#else
			fprintf(stderr, "no support for PKCS#11.\n");
#endif
			break;
		case 'J':
			if (options.jump_host != NULL)
				fatal("Only a single -J option permitted");
			if (options.proxy_command != NULL)
				fatal("Cannot specify -J with ProxyCommand");
			if (parse_jump(optarg, &options, 1) == -1)
				fatal("Invalid -J argument");
			options.proxy_command = xstrdup("none");
			break;
		case 't':
			if (options.request_tty == REQUEST_TTY_YES)
				options.request_tty = REQUEST_TTY_FORCE;
			else
				options.request_tty = REQUEST_TTY_YES;
			break;
		case 'v':
			if (debug_flag == 0) {
				debug_flag = 1;
				options.log_level = SYSLOG_LEVEL_DEBUG1;
			} else {
				if (options.log_level < SYSLOG_LEVEL_DEBUG3) {
					debug_flag++;
					options.log_level++;
				}
			}
			break;
		case 'V':
			if (options.version_addendum &&
			    *options.version_addendum != '\0')
				fprintf(stderr, "%s %s, %s\n", SSH_RELEASE,
				    options.version_addendum,
				    OPENSSL_VERSION_STRING);
			else
				fprintf(stderr, "%s, %s\n", SSH_RELEASE,
				    OPENSSL_VERSION_STRING);
			if (opt == 'V')
				exit(0);
			break;
		case 'w':
			if (options.tun_open == -1)
				options.tun_open = SSH_TUNMODE_DEFAULT;
			options.tun_local = a2tun(optarg, &options.tun_remote);
			if (options.tun_local == SSH_TUNID_ERR) {
				fprintf(stderr,
				    "Bad tun device '%s'\n", optarg);
				exit(255);
			}
			break;
		case 'W':
			if (options.stdio_forward_host != NULL)
				fatal("stdio forward already specified");
			if (muxclient_command != 0)
				fatal("Cannot specify stdio forward with -O");
			if (parse_forward(&fwd, optarg, 1, 0)) {
				options.stdio_forward_host = fwd.listen_host;
				options.stdio_forward_port = fwd.listen_port;
				free(fwd.connect_host);
			} else {
				fprintf(stderr,
				    "Bad stdio forwarding specification '%s'\n",
				    optarg);
				exit(255);
			}
			options.request_tty = REQUEST_TTY_NO;
			no_shell_flag = 1;
			break;
		case 'q':
			options.log_level = SYSLOG_LEVEL_QUIET;
			break;
		case 'e':
			if (optarg[0] == '^' && optarg[2] == 0 &&
			    (u_char) optarg[1] >= 64 &&
			    (u_char) optarg[1] < 128)
				options.escape_char = (u_char) optarg[1] & 31;
			else if (strlen(optarg) == 1)
				options.escape_char = (u_char) optarg[0];
			else if (strcmp(optarg, "none") == 0)
				options.escape_char = SSH_ESCAPECHAR_NONE;
			else {
				fprintf(stderr, "Bad escape character '%s'.\n",
				    optarg);
				exit(255);
			}
			break;
		case 'c':
			if (!ciphers_valid(*optarg == '+' ?
			    optarg + 1 : optarg)) {
				fprintf(stderr, "Unknown cipher type '%s'\n",
				    optarg);
				exit(255);
			}
			free(options.ciphers);
			options.ciphers = xstrdup(optarg);
			break;
		case 'm':
			if (mac_valid(optarg)) {
				free(options.macs);
				options.macs = xstrdup(optarg);
			} else {
				fprintf(stderr, "Unknown mac type '%s'\n",
				    optarg);
				exit(255);
			}
			break;
		case 'M':
			if (options.control_master == SSHCTL_MASTER_YES)
				options.control_master = SSHCTL_MASTER_ASK;
			else
				options.control_master = SSHCTL_MASTER_YES;
			break;
		case 'p':
			if (options.port == -1) {
				options.port = a2port(optarg);
				if (options.port <= 0) {
					fprintf(stderr, "Bad port '%s'\n",
					    optarg);
					exit(255);
				}
			}
			break;
		case 'l':
			if (options.user == NULL)
				options.user = optarg;
			break;

		case 'L':
			if (parse_forward(&fwd, optarg, 0, 0))
				add_local_forward(&options, &fwd);
			else {
				fprintf(stderr,
				    "Bad local forwarding specification '%s'\n",
				    optarg);
				exit(255);
			}
			break;

		case 'R':
			if (parse_forward(&fwd, optarg, 0, 1) ||
			    parse_forward(&fwd, optarg, 1, 1)) {
				add_remote_forward(&options, &fwd);
			} else {
				fprintf(stderr,
				    "Bad remote forwarding specification "
				    "'%s'\n", optarg);
				exit(255);
			}
			break;

		case 'D':
			if (parse_forward(&fwd, optarg, 1, 0)) {
				add_local_forward(&options, &fwd);
			} else {
				fprintf(stderr,
				    "Bad dynamic forwarding specification "
				    "'%s'\n", optarg);
				exit(255);
			}
			break;

		case 'C':
			options.compression = 1;
			break;
		case 'N':
			no_shell_flag = 1;
			options.request_tty = REQUEST_TTY_NO;
			break;
		case 'T':
			options.request_tty = REQUEST_TTY_NO;
			break;
		case 'o':
			line = xstrdup(optarg);
			if (process_config_line(&options, pw,
			    host ? host : "", host ? host : "", line,
			    "command-line", 0, NULL, SSHCONF_USERCONF) != 0)
				exit(255);
			free(line);
			break;
		case 's':
			subsystem_flag = 1;
			break;
		case 'S':
			free(options.control_path);
			options.control_path = xstrdup(optarg);
			break;
		case 'b':
			options.bind_address = optarg;
			break;
		case 'B':
			options.bind_interface = optarg;
			break;
		case 'F':
			config = optarg;
			break;
		default:
			usage();
		}
	}

	if (optind > 1 && strcmp(av[optind - 1], "--") == 0)
		opt_terminated = 1;

	ac -= optind;
	av += optind;

	if (ac > 0 && !host) {
		int tport;
		char *tuser;
		switch (parse_ssh_uri(*av, &tuser, &host, &tport)) {
		case -1:
			usage();
			break;
		case 0:
			if (options.user == NULL) {
				options.user = tuser;
				tuser = NULL;
			}
			free(tuser);
			if (options.port == -1 && tport != -1)
				options.port = tport;
			break;
		default:
			p = xstrdup(*av);
			cp = strrchr(p, '@');
			if (cp != NULL) {
				if (cp == p)
					usage();
				if (options.user == NULL) {
					options.user = p;
					p = NULL;
				}
				*cp++ = '\0';
				host = xstrdup(cp);
				free(p);
			} else
				host = p;
			break;
		}
		if (ac > 1 && !opt_terminated) {
			optind = optreset = 1;
			goto again;
		}
		ac--, av++;
	}

	/* Check that we got a host name. */
	if (!host)
		usage();

	host_arg = xstrdup(host);

#ifdef WITH_OPENSSL
	OpenSSL_add_all_algorithms();
	ERR_load_crypto_strings();
#endif

	/* Initialize the command to execute on remote host. */
	if ((command = sshbuf_new()) == NULL)
		fatal("sshbuf_new failed");

	/*
	 * Save the command to execute on the remote host in a buffer. There
	 * is no limit on the length of the command, except by the maximum
	 * packet size.  Also sets the tty flag if there is no command.
	 */
	if (!ac) {
		/* No command specified - execute shell on a tty. */
		if (subsystem_flag) {
			fprintf(stderr,
			    "You must specify a subsystem to invoke.\n");
			usage();
		}
	} else {
		/* A command has been specified.  Store it into the buffer. */
		for (i = 0; i < ac; i++) {
			if ((r = sshbuf_putf(command, "%s%s",
			    i ? " " : "", av[i])) != 0)
				fatal("%s: buffer error: %s",
				    __func__, ssh_err(r));
		}
	}

	/*
	 * Initialize "log" output.  Since we are the client all output
	 * goes to stderr unless otherwise specified by -y or -E.
	 */
	if (use_syslog && logfile != NULL)
		fatal("Can't specify both -y and -E");
	if (logfile != NULL)
		log_redirect_stderr_to(logfile);
	log_init(argv0,
	    options.log_level == SYSLOG_LEVEL_NOT_SET ?
	    SYSLOG_LEVEL_INFO : options.log_level,
	    options.log_facility == SYSLOG_FACILITY_NOT_SET ?
	    SYSLOG_FACILITY_USER : options.log_facility,
	    !use_syslog);

	if (debug_flag)
		/* version_addendum is always NULL at this point */
		logit("%s, %s", SSH_RELEASE, OPENSSL_VERSION_STRING);

	/* Parse the configuration files */
	process_config_files(host_arg, pw, 0);

	/* Hostname canonicalisation needs a few options filled. */
	fill_default_options_for_canonicalization(&options);

	/* If the user has replaced the hostname then take it into use now */
	if (options.hostname != NULL) {
		/* NB. Please keep in sync with readconf.c:match_cfg_line() */
		cp = percent_expand(options.hostname,
		    "h", host, (char *)NULL);
		free(host);
		host = cp;
		free(options.hostname);
		options.hostname = xstrdup(host);
	}

	/* Don't lowercase addresses, they will be explicitly canonicalised */
	if ((was_addr = is_addr(host)) == 0)
		lowercase(host);

	/*
	 * Try to canonicalize if requested by configuration or the
	 * hostname is an address.
	 */
	if (options.canonicalize_hostname != SSH_CANONICALISE_NO || was_addr)
		addrs = resolve_canonicalize(&host, options.port);

	/*
	 * If CanonicalizePermittedCNAMEs have been specified but
	 * other canonicalization did not happen (by not being requested
	 * or by failing with fallback) then the hostname may still be changed
	 * as a result of CNAME following.
	 *
	 * Try to resolve the bare hostname name using the system resolver's
	 * usual search rules and then apply the CNAME follow rules.
	 *
	 * Skip the lookup if a ProxyCommand is being used unless the user
	 * has specifically requested canonicalisation for this case via
	 * CanonicalizeHostname=always
	 */
	direct = option_clear_or_none(options.proxy_command) &&
	    options.jump_host == NULL;
	if (addrs == NULL && options.num_permitted_cnames != 0 && (direct ||
	    options.canonicalize_hostname == SSH_CANONICALISE_ALWAYS)) {
		if ((addrs = resolve_host(host, options.port,
		    option_clear_or_none(options.proxy_command),
		    cname, sizeof(cname))) == NULL) {
			/* Don't fatal proxied host names not in the DNS */
			if (option_clear_or_none(options.proxy_command))
				cleanup_exit(255); /* logged in resolve_host */
		} else
			check_follow_cname(direct, &host, cname);
	}

	/*
	 * If canonicalisation is enabled then re-parse the configuration
	 * files as new stanzas may match.
	 */
	if (options.canonicalize_hostname != 0) {
		debug("Re-reading configuration after hostname "
		    "canonicalisation");
		free(options.hostname);
		options.hostname = xstrdup(host);
		process_config_files(host_arg, pw, 1);
		/*
		 * Address resolution happens early with canonicalisation
		 * enabled and the port number may have changed since, so
		 * reset it in address list
		 */
		if (addrs != NULL && options.port > 0)
			set_addrinfo_port(addrs, options.port);
	}

	/* Fill configuration defaults. */
	fill_default_options(&options);

	/*
	 * If ProxyJump option specified, then construct a ProxyCommand now.
	 */
	if (options.jump_host != NULL) {
		char port_s[8];
		const char *sshbin = argv0;

		/*
		 * Try to use SSH indicated by argv[0], but fall back to
		 * "ssh" if it appears unavailable.
		 */
		if (strchr(argv0, '/') != NULL && access(argv0, X_OK) != 0)
			sshbin = "ssh";

		/* Consistency check */
		if (options.proxy_command != NULL)
			fatal("inconsistent options: ProxyCommand+ProxyJump");
		/* Never use FD passing for ProxyJump */
		options.proxy_use_fdpass = 0;
		snprintf(port_s, sizeof(port_s), "%d", options.jump_port);
		xasprintf(&options.proxy_command,
		    "%s%s%s%s%s%s%s%s%s%s%.*s -W '[%%h]:%%p' %s",
		    sshbin,
		    /* Optional "-l user" argument if jump_user set */
		    options.jump_user == NULL ? "" : " -l ",
		    options.jump_user == NULL ? "" : options.jump_user,
		    /* Optional "-p port" argument if jump_port set */
		    options.jump_port <= 0 ? "" : " -p ",
		    options.jump_port <= 0 ? "" : port_s,
		    /* Optional additional jump hosts ",..." */
		    options.jump_extra == NULL ? "" : " -J ",
		    options.jump_extra == NULL ? "" : options.jump_extra,
		    /* Optional "-F" argumment if -F specified */
		    config == NULL ? "" : " -F ",
		    config == NULL ? "" : config,
		    /* Optional "-v" arguments if -v set */
		    debug_flag ? " -" : "",
		    debug_flag, "vvv",
		    /* Mandatory hostname */
		    options.jump_host);
		debug("Setting implicit ProxyCommand from ProxyJump: %s",
		    options.proxy_command);
	}

	if (options.port == 0)
		options.port = default_ssh_port();
	channel_set_af(ssh, options.address_family);

	/* Tidy and check options */
	if (options.host_key_alias != NULL)
		lowercase(options.host_key_alias);
	if (options.proxy_command != NULL &&
	    strcmp(options.proxy_command, "-") == 0 &&
	    options.proxy_use_fdpass)
		fatal("ProxyCommand=- and ProxyUseFDPass are incompatible");
	if (options.control_persist &&
	    options.update_hostkeys == SSH_UPDATE_HOSTKEYS_ASK) {
		debug("UpdateHostKeys=ask is incompatible with ControlPersist; "
		    "disabling");
		options.update_hostkeys = 0;
	}
	if (options.connection_attempts <= 0)
		fatal("Invalid number of ConnectionAttempts");

	if (sshbuf_len(command) != 0 && options.remote_command != NULL)
		fatal("Cannot execute command-line and remote command.");

	/* Cannot fork to background if no command. */
	if (fork_after_authentication_flag && sshbuf_len(command) == 0 &&
	    options.remote_command == NULL && !no_shell_flag)
		fatal("Cannot fork into background without a command "
		    "to execute.");

	/* reinit */
	log_init(argv0, options.log_level, options.log_facility, !use_syslog);

	if (options.request_tty == REQUEST_TTY_YES ||
	    options.request_tty == REQUEST_TTY_FORCE)
		tty_flag = 1;

	/* Allocate a tty by default if no command specified. */
	if (sshbuf_len(command) == 0 && options.remote_command == NULL)
		tty_flag = options.request_tty != REQUEST_TTY_NO;

	/* Force no tty */
	if (options.request_tty == REQUEST_TTY_NO ||
	    (muxclient_command && muxclient_command != SSHMUX_COMMAND_PROXY))
		tty_flag = 0;
	/* Do not allocate a tty if stdin is not a tty. */
	if ((!isatty(fileno(stdin)) || stdin_null_flag) &&
	    options.request_tty != REQUEST_TTY_FORCE) {
		if (tty_flag)
			logit("Pseudo-terminal will not be allocated because "
			    "stdin is not a terminal.");
		tty_flag = 0;
	}

	seed_rng();

	if (options.user == NULL)
		options.user = xstrdup(pw->pw_name);

	/* Set up strings used to percent_expand() arguments */
	if (gethostname(thishost, sizeof(thishost)) == -1)
		fatal("gethostname: %s", strerror(errno));
	strlcpy(shorthost, thishost, sizeof(shorthost));
	shorthost[strcspn(thishost, ".")] = '\0';
	snprintf(portstr, sizeof(portstr), "%d", options.port);
	snprintf(uidstr, sizeof(uidstr), "%llu",
	    (unsigned long long)pw->pw_uid);

	/* Find canonic host name. */
	if (strchr(host, '.') == 0) {
		struct addrinfo hints;
		struct addrinfo *ai = NULL;
		int errgai;
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = options.address_family;
		hints.ai_flags = AI_CANONNAME;
		hints.ai_socktype = SOCK_STREAM;
		errgai = getaddrinfo(host, NULL, &hints, &ai);
		if (errgai == 0) {
			if (ai->ai_canonname != NULL)
				host = xstrdup(ai->ai_canonname);
			freeaddrinfo(ai);
		}
	}

	if ((md = ssh_digest_start(SSH_DIGEST_SHA1)) == NULL ||
	    ssh_digest_update(md, thishost, strlen(thishost)) < 0 ||
	    ssh_digest_update(md, host, strlen(host)) < 0 ||
	    ssh_digest_update(md, portstr, strlen(portstr)) < 0 ||
	    ssh_digest_update(md, options.user, strlen(options.user)) < 0 ||
	    ssh_digest_final(md, conn_hash, sizeof(conn_hash)) < 0)
		fatal("%s: mux digest failed", __func__);
	ssh_digest_free(md);
	conn_hash_hex = tohex(conn_hash, ssh_digest_bytes(SSH_DIGEST_SHA1));

	/*
	 * Expand tokens in arguments. NB. LocalCommand is expanded later,
	 * after port-forwarding is set up, so it may pick up any local
	 * tunnel interface name allocated.
	 */
	if (options.remote_command != NULL) {
		debug3("expanding RemoteCommand: %s", options.remote_command);
		cp = options.remote_command;
		options.remote_command = percent_expand(cp,
		    "C", conn_hash_hex,
		    "L", shorthost,
		    "d", pw->pw_dir,
		    "h", host,
		    "i", uidstr,
		    "l", thishost,
		    "n", host_arg,
		    "p", portstr,
		    "r", options.user,
		    "u", pw->pw_name,
		    (char *)NULL);
		debug3("expanded RemoteCommand: %s", options.remote_command);
		free(cp);
		if ((r = sshbuf_put(command, options.remote_command,
		    strlen(options.remote_command))) != 0)
			fatal("%s: buffer error: %s", __func__, ssh_err(r));
	}

	if (options.control_path != NULL) {
		cp = tilde_expand_filename(options.control_path, getuid());
		free(options.control_path);
		options.control_path = percent_expand(cp,
		    "C", conn_hash_hex,
		    "L", shorthost,
		    "h", host,
		    "i", uidstr,
		    "l", thishost,
		    "n", host_arg,
		    "p", portstr,
		    "r", options.user,
		    "u", pw->pw_name,
		    "i", uidstr,
		    (char *)NULL);
		free(cp);
	}

	if (config_test) {
		dump_client_config(&options, host);
		exit(0);
	}

	if (muxclient_command != 0 && options.control_path == NULL)
		fatal("No ControlPath specified for \"-O\" command");
	if (options.control_path != NULL) {
		int sock;
		if ((sock = muxclient(options.control_path)) >= 0) {
			ssh_packet_set_connection(ssh, sock, sock);
			packet_set_mux();
			goto skip_connect;
		}
	}

	/*
	 * If hostname canonicalisation was not enabled, then we may not
	 * have yet resolved the hostname. Do so now.
	 */
	if (addrs == NULL && options.proxy_command == NULL) {
		debug2("resolving \"%s\" port %d", host, options.port);
		if ((addrs = resolve_host(host, options.port, 1,
		    cname, sizeof(cname))) == NULL)
			cleanup_exit(255); /* resolve_host logs the error */
	}

	timeout_ms = options.connection_timeout * 1000;

	/* Open a connection to the remote host. */
	if (ssh_connect(ssh, host, addrs, &hostaddr, options.port,
	    options.address_family, options.connection_attempts,
	    &timeout_ms, options.tcp_keep_alive) != 0)
 		exit(255);

	if (addrs != NULL)
		freeaddrinfo(addrs);

	packet_set_timeout(options.server_alive_interval,
	    options.server_alive_count_max);

	ssh = active_state; /* XXX */

	if (timeout_ms > 0)
		debug3("timeout: %d ms remain after connect", timeout_ms);

	/*
	 * If we successfully made the connection and we have hostbased auth
	 * enabled, load the public keys so we can later use the ssh-keysign
	 * helper to sign challenges.
	 */
	sensitive_data.nkeys = 0;
	sensitive_data.keys = NULL;
	if (options.hostbased_authentication) {
		sensitive_data.nkeys = 10;
		sensitive_data.keys = xcalloc(sensitive_data.nkeys,
		    sizeof(struct sshkey));

		/* XXX check errors? */
#define L_PUBKEY(p,o) do { \
	if ((o) >= sensitive_data.nkeys) \
		fatal("%s pubkey out of array bounds", __func__); \
	check_load(sshkey_load_public(p, &(sensitive_data.keys[o]), NULL), \
	    p, "pubkey"); \
} while (0)
#define L_CERT(p,o) do { \
	if ((o) >= sensitive_data.nkeys) \
		fatal("%s cert out of array bounds", __func__); \
	check_load(sshkey_load_cert(p, &(sensitive_data.keys[o])), p, "cert"); \
} while (0)

		if (options.hostbased_authentication == 1) {
			L_CERT(_PATH_HOST_ECDSA_KEY_FILE, 0);
			L_CERT(_PATH_HOST_ED25519_KEY_FILE, 1);
			L_CERT(_PATH_HOST_RSA_KEY_FILE, 2);
			L_CERT(_PATH_HOST_DSA_KEY_FILE, 3);
			L_PUBKEY(_PATH_HOST_ECDSA_KEY_FILE, 4);
			L_PUBKEY(_PATH_HOST_ED25519_KEY_FILE, 5);
			L_PUBKEY(_PATH_HOST_RSA_KEY_FILE, 6);
			L_PUBKEY(_PATH_HOST_DSA_KEY_FILE, 7);
			L_CERT(_PATH_HOST_XMSS_KEY_FILE, 8);
			L_PUBKEY(_PATH_HOST_XMSS_KEY_FILE, 9);
		}
	}

	/* Create ~/.ssh * directory if it doesn't already exist. */
	if (config == NULL) {
		r = snprintf(buf, sizeof buf, "%s%s%s", pw->pw_dir,
		    strcmp(pw->pw_dir, "/") ? "/" : "", _PATH_SSH_USER_DIR);
		if (r > 0 && (size_t)r < sizeof(buf) && stat(buf, &st) < 0) {
#ifdef WITH_SELINUX
			ssh_selinux_setfscreatecon(buf);
#endif
			if (mkdir(buf, 0700) < 0)
				error("Could not create directory '%.200s'.",
				    buf);
#ifdef WITH_SELINUX
			ssh_selinux_setfscreatecon(NULL);
#endif
		}
	}
	/* load options.identity_files */
	load_public_identity_files(pw);

	/* optionally set the SSH_AUTHSOCKET_ENV_NAME variable */
	if (options.identity_agent &&
	    strcmp(options.identity_agent, SSH_AUTHSOCKET_ENV_NAME) != 0) {
		if (strcmp(options.identity_agent, "none") == 0) {
			unsetenv(SSH_AUTHSOCKET_ENV_NAME);
		} else {
			p = tilde_expand_filename(options.identity_agent,
			    getuid());
			cp = percent_expand(p,
			    "d", pw->pw_dir,
			    "h", host,
			    "i", uidstr,
			    "l", thishost,
			    "r", options.user,
			    "u", pw->pw_name,
			    (char *)NULL);
			setenv(SSH_AUTHSOCKET_ENV_NAME, cp, 1);
			free(cp);
			free(p);
		}
	}

	/* Expand ~ in known host file names. */
	tilde_expand_paths(options.system_hostfiles,
	    options.num_system_hostfiles);
	tilde_expand_paths(options.user_hostfiles, options.num_user_hostfiles);

	signal(SIGPIPE, SIG_IGN); /* ignore SIGPIPE early */
	signal(SIGCHLD, main_sigchld_handler);

	/* Log into the remote system.  Never returns if the login fails. */
	ssh_login(&sensitive_data, host, (struct sockaddr *)&hostaddr,
	    options.port, pw, timeout_ms);

	if (packet_connection_is_on_socket()) {
		verbose("Authenticated to %s ([%s]:%d).", host,
		    ssh_remote_ipaddr(ssh), ssh_remote_port(ssh));
	} else {
		verbose("Authenticated to %s (via proxy).", host);
	}

	/* We no longer need the private host keys.  Clear them now. */
	if (sensitive_data.nkeys != 0) {
		for (i = 0; i < sensitive_data.nkeys; i++) {
			if (sensitive_data.keys[i] != NULL) {
				/* Destroys contents safely */
				debug3("clear hostkey %d", i);
				sshkey_free(sensitive_data.keys[i]);
				sensitive_data.keys[i] = NULL;
			}
		}
		free(sensitive_data.keys);
	}
	for (i = 0; i < options.num_identity_files; i++) {
		free(options.identity_files[i]);
		options.identity_files[i] = NULL;
		if (options.identity_keys[i]) {
			sshkey_free(options.identity_keys[i]);
			options.identity_keys[i] = NULL;
		}
	}
	for (i = 0; i < options.num_certificate_files; i++) {
		free(options.certificate_files[i]);
		options.certificate_files[i] = NULL;
	}

 skip_connect:
	exit_status = ssh_session2(ssh, pw);
	packet_close();

	if (options.control_path != NULL && muxserver_sock != -1)
		unlink(options.control_path);

	/* Kill ProxyCommand if it is running. */
	ssh_kill_proxy_command();

	return exit_status;
}

static void
control_persist_detach(void)
{
	pid_t pid;
	int devnull, keep_stderr;

	debug("%s: backgrounding master process", __func__);

	/*
	 * master (current process) into the background, and make the
	 * foreground process a client of the backgrounded master.
	 */
	switch ((pid = fork())) {
	case -1:
		fatal("%s: fork: %s", __func__, strerror(errno));
	case 0:
		/* Child: master process continues mainloop */
		break;
	default:
		/* Parent: set up mux slave to connect to backgrounded master */
		debug2("%s: background process is %ld", __func__, (long)pid);
		stdin_null_flag = ostdin_null_flag;
		options.request_tty = orequest_tty;
		tty_flag = otty_flag;
		close(muxserver_sock);
		muxserver_sock = -1;
		options.control_master = SSHCTL_MASTER_NO;
		muxclient(options.control_path);
		/* muxclient() doesn't return on success. */
		fatal("Failed to connect to new control master");
	}
	if ((devnull = open(_PATH_DEVNULL, O_RDWR)) == -1) {
		error("%s: open(\"/dev/null\"): %s", __func__,
		    strerror(errno));
	} else {
		keep_stderr = log_is_on_stderr() && debug_flag;
		if (dup2(devnull, STDIN_FILENO) == -1 ||
		    dup2(devnull, STDOUT_FILENO) == -1 ||
		    (!keep_stderr && dup2(devnull, STDERR_FILENO) == -1))
			error("%s: dup2: %s", __func__, strerror(errno));
		if (devnull > STDERR_FILENO)
			close(devnull);
	}
	daemon(1, 1);
	setproctitle("%s [mux]", options.control_path);
}

/* Do fork() after authentication. Used by "ssh -f" */
static void
fork_postauth(void)
{
	if (need_controlpersist_detach)
		control_persist_detach();
	debug("forking to background");
	fork_after_authentication_flag = 0;
	if (daemon(1, 1) < 0)
		fatal("daemon() failed: %.200s", strerror(errno));
}

/* Callback for remote forward global requests */
static void
ssh_confirm_remote_forward(struct ssh *ssh, int type, u_int32_t seq, void *ctxt)
{
	struct Forward *rfwd = (struct Forward *)ctxt;

	/* XXX verbose() on failure? */
	debug("remote forward %s for: listen %s%s%d, connect %s:%d",
	    type == SSH2_MSG_REQUEST_SUCCESS ? "success" : "failure",
	    rfwd->listen_path ? rfwd->listen_path :
	    rfwd->listen_host ? rfwd->listen_host : "",
	    (rfwd->listen_path || rfwd->listen_host) ? ":" : "",
	    rfwd->listen_port, rfwd->connect_path ? rfwd->connect_path :
	    rfwd->connect_host, rfwd->connect_port);
	if (rfwd->listen_path == NULL && rfwd->listen_port == 0) {
		if (type == SSH2_MSG_REQUEST_SUCCESS) {
			rfwd->allocated_port = packet_get_int();
			logit("Allocated port %u for remote forward to %s:%d",
			    rfwd->allocated_port,
			    rfwd->connect_host, rfwd->connect_port);
			channel_update_permission(ssh,
			    rfwd->handle, rfwd->allocated_port);
		} else {
			channel_update_permission(ssh, rfwd->handle, -1);
		}
	}

	if (type == SSH2_MSG_REQUEST_FAILURE) {
		if (options.exit_on_forward_failure) {
			if (rfwd->listen_path != NULL)
				fatal("Error: remote port forwarding failed "
				    "for listen path %s", rfwd->listen_path);
			else
				fatal("Error: remote port forwarding failed "
				    "for listen port %d", rfwd->listen_port);
		} else {
			if (rfwd->listen_path != NULL)
				logit("Warning: remote port forwarding failed "
				    "for listen path %s", rfwd->listen_path);
			else
				logit("Warning: remote port forwarding failed "
				    "for listen port %d", rfwd->listen_port);
		}
	}
	if (++remote_forward_confirms_received == options.num_remote_forwards) {
		debug("All remote forwarding requests processed");
		if (fork_after_authentication_flag)
			fork_postauth();
	}
}

static void
client_cleanup_stdio_fwd(struct ssh *ssh, int id, void *arg)
{
	debug("stdio forwarding: done");
	cleanup_exit(0);
}

static void
ssh_stdio_confirm(struct ssh *ssh, int id, int success, void *arg)
{
	if (!success)
		fatal("stdio forwarding failed");
}

static void
ssh_init_stdio_forwarding(struct ssh *ssh)
{
	Channel *c;
	int in, out;

	if (options.stdio_forward_host == NULL)
		return;

	debug3("%s: %s:%d", __func__, options.stdio_forward_host,
	    options.stdio_forward_port);

	if ((in = dup(STDIN_FILENO)) < 0 ||
	    (out = dup(STDOUT_FILENO)) < 0)
		fatal("channel_connect_stdio_fwd: dup() in/out failed");
	if ((c = channel_connect_stdio_fwd(ssh, options.stdio_forward_host,
	    options.stdio_forward_port, in, out)) == NULL)
		fatal("%s: channel_connect_stdio_fwd failed", __func__);
	channel_register_cleanup(ssh, c->self, client_cleanup_stdio_fwd, 0);
	channel_register_open_confirm(ssh, c->self, ssh_stdio_confirm, NULL);
}

static void
ssh_init_forwarding(struct ssh *ssh, char **ifname)
{
	int success = 0;
	int i;

	/* Initiate local TCP/IP port forwardings. */
	for (i = 0; i < options.num_local_forwards; i++) {
		debug("Local connections to %.200s:%d forwarded to remote "
		    "address %.200s:%d",
		    (options.local_forwards[i].listen_path != NULL) ?
		    options.local_forwards[i].listen_path :
		    (options.local_forwards[i].listen_host == NULL) ?
		    (options.fwd_opts.gateway_ports ? "*" : "LOCALHOST") :
		    options.local_forwards[i].listen_host,
		    options.local_forwards[i].listen_port,
		    (options.local_forwards[i].connect_path != NULL) ?
		    options.local_forwards[i].connect_path :
		    options.local_forwards[i].connect_host,
		    options.local_forwards[i].connect_port);
		success += channel_setup_local_fwd_listener(ssh,
		    &options.local_forwards[i], &options.fwd_opts);
	}
	if (i > 0 && success != i && options.exit_on_forward_failure)
		fatal("Could not request local forwarding.");
	if (i > 0 && success == 0)
		error("Could not request local forwarding.");

	/* Initiate remote TCP/IP port forwardings. */
	for (i = 0; i < options.num_remote_forwards; i++) {
		debug("Remote connections from %.200s:%d forwarded to "
		    "local address %.200s:%d",
		    (options.remote_forwards[i].listen_path != NULL) ?
		    options.remote_forwards[i].listen_path :
		    (options.remote_forwards[i].listen_host == NULL) ?
		    "LOCALHOST" : options.remote_forwards[i].listen_host,
		    options.remote_forwards[i].listen_port,
		    (options.remote_forwards[i].connect_path != NULL) ?
		    options.remote_forwards[i].connect_path :
		    options.remote_forwards[i].connect_host,
		    options.remote_forwards[i].connect_port);
		options.remote_forwards[i].handle =
		    channel_request_remote_forwarding(ssh,
		    &options.remote_forwards[i]);
		if (options.remote_forwards[i].handle < 0) {
			if (options.exit_on_forward_failure)
				fatal("Could not request remote forwarding.");
			else
				logit("Warning: Could not request remote "
				    "forwarding.");
		} else {
			client_register_global_confirm(
			    ssh_confirm_remote_forward,
			    &options.remote_forwards[i]);
		}
	}

	/* Initiate tunnel forwarding. */
	if (options.tun_open != SSH_TUNMODE_NO) {
		if ((*ifname = client_request_tun_fwd(ssh,
		    options.tun_open, options.tun_local,
		    options.tun_remote)) == NULL) {
			if (options.exit_on_forward_failure)
				fatal("Could not request tunnel forwarding.");
			else
				error("Could not request tunnel forwarding.");
		}
	}
}

static void
check_agent_present(void)
{
	int r;

	if (options.forward_agent) {
		/* Clear agent forwarding if we don't have an agent. */
		if ((r = ssh_get_authentication_socket(NULL)) != 0) {
			options.forward_agent = 0;
			if (r != SSH_ERR_AGENT_NOT_PRESENT)
				debug("ssh_get_authentication_socket: %s",
				    ssh_err(r));
		}
	}
}

static void
ssh_session2_setup(struct ssh *ssh, int id, int success, void *arg)
{
	extern char **environ;
	const char *display;
	int interactive = tty_flag;
	char *proto = NULL, *data = NULL;

	if (!success)
		return; /* No need for error message, channels code sens one */

	display = getenv("DISPLAY");
	if (display == NULL && options.forward_x11)
		debug("X11 forwarding requested but DISPLAY not set");
	if (options.forward_x11 && client_x11_get_proto(ssh, display,
	    options.xauth_location, options.forward_x11_trusted,
	    options.forward_x11_timeout, &proto, &data) == 0) {
		/* Request forwarding with authentication spoofing. */
		debug("Requesting X11 forwarding with authentication "
		    "spoofing.");
		x11_request_forwarding_with_spoofing(ssh, id, display, proto,
		    data, 1);
		client_expect_confirm(ssh, id, "X11 forwarding", CONFIRM_WARN);
		/* XXX exit_on_forward_failure */
		interactive = 1;
	}

	check_agent_present();
	if (options.forward_agent) {
		debug("Requesting authentication agent forwarding.");
		channel_request_start(ssh, id, "auth-agent-req@openssh.com", 0);
		packet_send();
	}

	/* Tell the packet module whether this is an interactive session. */
	packet_set_interactive(interactive,
	    options.ip_qos_interactive, options.ip_qos_bulk);

	client_session2_setup(ssh, id, tty_flag, subsystem_flag, getenv("TERM"),
	    NULL, fileno(stdin), command, environ);
}

/* open new channel for a session */
static int
ssh_session2_open(struct ssh *ssh)
{
	Channel *c;
	int window, packetmax, in, out, err;

	if (stdin_null_flag) {
		in = open(_PATH_DEVNULL, O_RDONLY);
	} else {
		in = dup(STDIN_FILENO);
	}
	out = dup(STDOUT_FILENO);
	err = dup(STDERR_FILENO);

	if (in < 0 || out < 0 || err < 0)
		fatal("dup() in/out/err failed");

	/* enable nonblocking unless tty */
	if (!isatty(in))
		set_nonblock(in);
	if (!isatty(out))
		set_nonblock(out);
	if (!isatty(err))
		set_nonblock(err);

	window = CHAN_SES_WINDOW_DEFAULT;
	packetmax = CHAN_SES_PACKET_DEFAULT;
	if (tty_flag) {
		window >>= 1;
		packetmax >>= 1;
	}
	c = channel_new(ssh,
	    "session", SSH_CHANNEL_OPENING, in, out, err,
	    window, packetmax, CHAN_EXTENDED_WRITE,
	    "client-session", /*nonblock*/0);

	debug3("%s: channel_new: %d", __func__, c->self);

	channel_send_open(ssh, c->self);
	if (!no_shell_flag)
		channel_register_open_confirm(ssh, c->self,
		    ssh_session2_setup, NULL);

	return c->self;
}

static int
ssh_session2(struct ssh *ssh, struct passwd *pw)
{
	int devnull, id = -1;
	char *cp, *tun_fwd_ifname = NULL;

	/* XXX should be pre-session */
	if (!options.control_persist)
		ssh_init_stdio_forwarding(ssh);

	ssh_init_forwarding(ssh, &tun_fwd_ifname);

	if (options.local_command != NULL) {
		debug3("expanding LocalCommand: %s", options.local_command);
		cp = options.local_command;
		options.local_command = percent_expand(cp,
		    "C", conn_hash_hex,
		    "L", shorthost,
		    "d", pw->pw_dir,
		    "h", host,
		    "i", uidstr,
		    "l", thishost,
		    "n", host_arg,
		    "p", portstr,
		    "r", options.user,
		    "u", pw->pw_name,
		    "T", tun_fwd_ifname == NULL ? "NONE" : tun_fwd_ifname,
		    (char *)NULL);
		debug3("expanded LocalCommand: %s", options.local_command);
		free(cp);
	}

	/* Start listening for multiplex clients */
	if (!packet_get_mux())
		muxserver_listen(ssh);

	/*
	 * If we are in control persist mode and have a working mux listen
	 * socket, then prepare to background ourselves and have a foreground
	 * client attach as a control slave.
	 * NB. we must save copies of the flags that we override for
	 * the backgrounding, since we defer attachment of the slave until
	 * after the connection is fully established (in particular,
	 * async rfwd replies have been received for ExitOnForwardFailure).
	 */
	if (options.control_persist && muxserver_sock != -1) {
		ostdin_null_flag = stdin_null_flag;
		ono_shell_flag = no_shell_flag;
		orequest_tty = options.request_tty;
		otty_flag = tty_flag;
		stdin_null_flag = 1;
		no_shell_flag = 1;
		tty_flag = 0;
		if (!fork_after_authentication_flag)
			need_controlpersist_detach = 1;
		fork_after_authentication_flag = 1;
	}
	/*
	 * ControlPersist mux listen socket setup failed, attempt the
	 * stdio forward setup that we skipped earlier.
	 */
	if (options.control_persist && muxserver_sock == -1)
		ssh_init_stdio_forwarding(ssh);

	if (!no_shell_flag)
		id = ssh_session2_open(ssh);
	else {
		packet_set_interactive(
		    options.control_master == SSHCTL_MASTER_NO,
		    options.ip_qos_interactive, options.ip_qos_bulk);
	}

	/* If we don't expect to open a new session, then disallow it */
	if (options.control_master == SSHCTL_MASTER_NO &&
	    (datafellows & SSH_NEW_OPENSSH)) {
		debug("Requesting no-more-sessions@openssh.com");
		packet_start(SSH2_MSG_GLOBAL_REQUEST);
		packet_put_cstring("no-more-sessions@openssh.com");
		packet_put_char(0);
		packet_send();
	}

	/* Execute a local command */
	if (options.local_command != NULL &&
	    options.permit_local_command)
		ssh_local_cmd(options.local_command);

	/*
	 * stdout is now owned by the session channel; clobber it here
	 * so future channel closes are propagated to the local fd.
	 * NB. this can only happen after LocalCommand has completed,
	 * as it may want to write to stdout.
	 */
	if (!need_controlpersist_detach) {
		if ((devnull = open(_PATH_DEVNULL, O_WRONLY)) == -1)
			error("%s: open %s: %s", __func__,
			    _PATH_DEVNULL, strerror(errno));
		if (dup2(devnull, STDOUT_FILENO) < 0)
			fatal("%s: dup2() stdout failed", __func__);
		if (devnull > STDERR_FILENO)
			close(devnull);
	}

	/*
	 * If requested and we are not interested in replies to remote
	 * forwarding requests, then let ssh continue in the background.
	 */
	if (fork_after_authentication_flag) {
		if (options.exit_on_forward_failure &&
		    options.num_remote_forwards > 0) {
			debug("deferring postauth fork until remote forward "
			    "confirmation received");
		} else
			fork_postauth();
	}

	return client_loop(ssh, tty_flag, tty_flag ?
	    options.escape_char : SSH_ESCAPECHAR_NONE, id);
}

/* Loads all IdentityFile and CertificateFile keys */
static void
load_public_identity_files(struct passwd *pw)
{
	char *filename, *cp;
	struct sshkey *public;
	int i;
	u_int n_ids, n_certs;
	char *identity_files[SSH_MAX_IDENTITY_FILES];
	struct sshkey *identity_keys[SSH_MAX_IDENTITY_FILES];
	int identity_file_userprovided[SSH_MAX_IDENTITY_FILES];
	char *certificate_files[SSH_MAX_CERTIFICATE_FILES];
	struct sshkey *certificates[SSH_MAX_CERTIFICATE_FILES];
	int certificate_file_userprovided[SSH_MAX_CERTIFICATE_FILES];
#ifdef ENABLE_PKCS11
	struct sshkey **keys;
	int nkeys;
#endif /* PKCS11 */

	n_ids = n_certs = 0;
	memset(identity_files, 0, sizeof(identity_files));
	memset(identity_keys, 0, sizeof(identity_keys));
	memset(identity_file_userprovided, 0,
	    sizeof(identity_file_userprovided));
	memset(certificate_files, 0, sizeof(certificate_files));
	memset(certificates, 0, sizeof(certificates));
	memset(certificate_file_userprovided, 0,
	    sizeof(certificate_file_userprovided));

#ifdef ENABLE_PKCS11
	if (options.pkcs11_provider != NULL &&
	    options.num_identity_files < SSH_MAX_IDENTITY_FILES &&
	    (pkcs11_init(!options.batch_mode) == 0) &&
	    (nkeys = pkcs11_add_provider(options.pkcs11_provider, NULL,
	    &keys)) > 0) {
		for (i = 0; i < nkeys; i++) {
			if (n_ids >= SSH_MAX_IDENTITY_FILES) {
				sshkey_free(keys[i]);
				continue;
			}
			identity_keys[n_ids] = keys[i];
			identity_files[n_ids] =
			    xstrdup(options.pkcs11_provider); /* XXX */
			n_ids++;
		}
		free(keys);
	}
#endif /* ENABLE_PKCS11 */
	for (i = 0; i < options.num_identity_files; i++) {
		if (n_ids >= SSH_MAX_IDENTITY_FILES ||
		    strcasecmp(options.identity_files[i], "none") == 0) {
			free(options.identity_files[i]);
			options.identity_files[i] = NULL;
			continue;
		}
		cp = tilde_expand_filename(options.identity_files[i], getuid());
		filename = percent_expand(cp, "d", pw->pw_dir,
		    "u", pw->pw_name, "l", thishost, "h", host,
		    "r", options.user, (char *)NULL);
		free(cp);
		check_load(sshkey_load_public(filename, &public, NULL),
		    filename, "pubkey");
		debug("identity file %s type %d", filename,
		    public ? public->type : -1);
		free(options.identity_files[i]);
		identity_files[n_ids] = filename;
		identity_keys[n_ids] = public;
		identity_file_userprovided[n_ids] =
		    options.identity_file_userprovided[i];
		if (++n_ids >= SSH_MAX_IDENTITY_FILES)
			continue;

		/*
		 * If no certificates have been explicitly listed then try
		 * to add the default certificate variant too.
		 */
		if (options.num_certificate_files != 0)
			continue;
		xasprintf(&cp, "%s-cert", filename);
		check_load(sshkey_load_public(cp, &public, NULL),
		    filename, "pubkey");
		debug("identity file %s type %d", cp,
		    public ? public->type : -1);
		if (public == NULL) {
			free(cp);
			continue;
		}
		if (!sshkey_is_cert(public)) {
			debug("%s: key %s type %s is not a certificate",
			    __func__, cp, sshkey_type(public));
			sshkey_free(public);
			free(cp);
			continue;
		}
		/* NB. leave filename pointing to private key */
		identity_files[n_ids] = xstrdup(filename);
		identity_keys[n_ids] = public;
		identity_file_userprovided[n_ids] =
		    options.identity_file_userprovided[i];
		n_ids++;
	}

	if (options.num_certificate_files > SSH_MAX_CERTIFICATE_FILES)
		fatal("%s: too many certificates", __func__);
	for (i = 0; i < options.num_certificate_files; i++) {
		cp = tilde_expand_filename(options.certificate_files[i],
		    getuid());
		filename = percent_expand(cp,
		    "d", pw->pw_dir,
		    "h", host,
		    "i", uidstr,
		    "l", thishost,
		    "r", options.user,
		    "u", pw->pw_name,
		    (char *)NULL);
		free(cp);

		check_load(sshkey_load_public(filename, &public, NULL),
		    filename, "certificate");
		debug("certificate file %s type %d", filename,
		    public ? public->type : -1);
		free(options.certificate_files[i]);
		options.certificate_files[i] = NULL;
		if (public == NULL) {
			free(filename);
			continue;
		}
		if (!sshkey_is_cert(public)) {
			debug("%s: key %s type %s is not a certificate",
			    __func__, filename, sshkey_type(public));
			sshkey_free(public);
			free(filename);
			continue;
		}
		certificate_files[n_certs] = filename;
		certificates[n_certs] = public;
		certificate_file_userprovided[n_certs] =
		    options.certificate_file_userprovided[i];
		++n_certs;
	}

	options.num_identity_files = n_ids;
	memcpy(options.identity_files, identity_files, sizeof(identity_files));
	memcpy(options.identity_keys, identity_keys, sizeof(identity_keys));
	memcpy(options.identity_file_userprovided,
	    identity_file_userprovided, sizeof(identity_file_userprovided));

	options.num_certificate_files = n_certs;
	memcpy(options.certificate_files,
	    certificate_files, sizeof(certificate_files));
	memcpy(options.certificates, certificates, sizeof(certificates));
	memcpy(options.certificate_file_userprovided,
	    certificate_file_userprovided,
	    sizeof(certificate_file_userprovided));
}

static void
main_sigchld_handler(int sig)
{
	int save_errno = errno;
	pid_t pid;
	int status;

	while ((pid = waitpid(-1, &status, WNOHANG)) > 0 ||
	    (pid < 0 && errno == EINTR))
		;
	errno = save_errno;
}
