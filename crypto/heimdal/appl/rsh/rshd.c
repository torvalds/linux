/*
 * Copyright (c) 1997-2007 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "rsh_locl.h"
#include "login_locl.h"
RCSID("$Id$");

int
login_access( struct passwd *user, char *from);
int
read_limits_conf(const char *file, const struct passwd *pwd);

#ifdef NEED_IRUSEROK_PROTO
int iruserok(uint32_t, int, const char *, const char *);
#endif

enum auth_method auth_method;

#ifdef KRB5
krb5_context context;
krb5_keyblock *keyblock;
krb5_crypto crypto;
#endif

#ifdef KRB5
krb5_ccache ccache, ccache2;
int kerberos_status = 0;
#endif

int do_encrypt = 0;

static int do_unique_tkfile           = 0;
static char tkfile[MAXPATHLEN] = "";

static int do_inetd = 1;
static char *port_str;
static int do_rhosts = 1;
static int do_kerberos = 0;
#define DO_KRB5 4
static int do_vacuous = 0;
static int do_log = 1;
static int do_newpag = 1;
static int do_addr_verify = 0;
static int do_keepalive = 1;
static int do_version;
static int do_help = 0;

static void
syslog_and_die (const char *m, ...)
    __attribute__ ((format (printf, 1, 2)));

static void
syslog_and_die (const char *m, ...)
{
    va_list args;

    va_start(args, m);
    vsyslog (LOG_ERR, m, args);
    va_end(args);
    exit (1);
}

static void
fatal (int, const char*, const char *, ...)
    __attribute__ ((noreturn, format (printf, 3, 4)));

static void
fatal (int sock, const char *what, const char *m, ...)
{
    va_list args;
    char buf[BUFSIZ];
    size_t len;

    *buf = 1;
    va_start(args, m);
    len = vsnprintf (buf + 1, sizeof(buf) - 1, m, args);
    len = min(len, sizeof(buf) - 1);
    va_end(args);
    if(what != NULL)
	syslog (LOG_ERR, "%s: %s: %s", what, strerror(errno), buf + 1);
    else
	syslog (LOG_ERR, "%s", buf + 1);
    net_write (sock, buf, len + 1);
    exit (1);
}

static char *
read_str (int s, size_t sz, char *expl)
{
    char *str = malloc(sz);
    char *p = str;
    if(str == NULL)
	fatal(s, NULL, "%s too long", expl);
    while(p < str + sz) {
	if(net_read(s, p, 1) != 1)
	    syslog_and_die("read: %s", strerror(errno));
	if(*p == '\0')
	    return str;
	p++;
    }
    fatal(s, NULL, "%s too long", expl);
}

static int
recv_bsd_auth (int s, u_char *buf,
	       struct sockaddr_in *thisaddr,
	       struct sockaddr_in *thataddr,
	       char **client_username,
	       char **server_username,
	       char **cmd)
{
    struct passwd *pwd;

    *client_username = read_str (s, USERNAME_SZ, "local username");
    *server_username = read_str (s, USERNAME_SZ, "remote username");
    *cmd = read_str (s, ARG_MAX + 1, "command");
    pwd = getpwnam(*server_username);
    if (pwd == NULL)
	fatal(s, NULL, "Login incorrect.");
    if (iruserok(thataddr->sin_addr.s_addr, pwd->pw_uid == 0,
		 *client_username, *server_username))
	fatal(s, NULL, "Login incorrect.");
    return 0;
}

#ifdef KRB5
static int
save_krb5_creds (int s,
                 krb5_auth_context auth_context,
                 krb5_principal client)

{
    int ret;
    krb5_data remote_cred;

    krb5_data_zero (&remote_cred);
    ret= krb5_read_message (context, (void *)&s, &remote_cred);
    if (ret) {
	krb5_data_free(&remote_cred);
	return 0;
    }
    if (remote_cred.length == 0)
	return 0;

    ret = krb5_cc_new_unique(context, krb5_cc_type_memory, NULL, &ccache);
    if (ret) {
	krb5_data_free(&remote_cred);
	return 0;
    }

    krb5_cc_initialize(context,ccache,client);
    ret = krb5_rd_cred2(context, auth_context, ccache, &remote_cred);
    if(ret != 0)
	syslog(LOG_INFO|LOG_AUTH,
	       "reading creds: %s", krb5_get_err_text(context, ret));
    krb5_data_free (&remote_cred);
    if (ret)
	return 0;
    return 1;
}

static void
krb5_start_session (void)
{
    krb5_error_code ret;
    char *estr;

    ret = krb5_cc_resolve (context, tkfile, &ccache2);
    if (ret) {
	estr = krb5_get_error_string(context);
	syslog(LOG_WARNING, "resolve cred cache %s: %s",
	       tkfile,
	       estr ? estr : krb5_get_err_text(context, ret));
	free(estr);
	krb5_cc_destroy(context, ccache);
	return;
    }

    ret = krb5_cc_copy_cache (context, ccache, ccache2);
    if (ret) {
	estr = krb5_get_error_string(context);
	syslog(LOG_WARNING, "storing credentials: %s",
	       estr ? estr : krb5_get_err_text(context, ret));
	free(estr);
	krb5_cc_destroy(context, ccache);
	return ;
    }

    krb5_cc_close(context, ccache2);
    krb5_cc_destroy(context, ccache);
    return;
}

static int protocol_version;

static krb5_boolean
match_kcmd_version(const void *data, const char *version)
{
    if(strcmp(version, KCMD_NEW_VERSION) == 0) {
	protocol_version = 2;
	return TRUE;
    }
    if(strcmp(version, KCMD_OLD_VERSION) == 0) {
	protocol_version = 1;
	key_usage = KRB5_KU_OTHER_ENCRYPTED;
	return TRUE;
    }
    return FALSE;
}


static int
recv_krb5_auth (int s, u_char *buf,
		struct sockaddr *thisaddr,
		struct sockaddr *thataddr,
		char **client_username,
		char **server_username,
		char **cmd)
{
    uint32_t len;
    krb5_auth_context auth_context = NULL;
    krb5_ticket *ticket;
    krb5_error_code status;
    krb5_data cksum_data;
    krb5_principal server;
    char *str;

    if (memcmp (buf, "\x00\x00\x00\x13", 4) != 0)
	return -1;
    len = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | (buf[3]);

    if (net_read(s, buf, len) != len)
	syslog_and_die ("reading auth info: %s", strerror(errno));
    if (len != sizeof(KRB5_SENDAUTH_VERSION)
	|| memcmp (buf, KRB5_SENDAUTH_VERSION, len) != 0)
	syslog_and_die ("bad sendauth version: %.8s", buf);

    status = krb5_sock_to_principal (context,
				     s,
				     "host",
				     KRB5_NT_SRV_HST,
				     &server);
    if (status)
	syslog_and_die ("krb5_sock_to_principal: %s",
			krb5_get_err_text(context, status));

    status = krb5_recvauth_match_version(context,
					 &auth_context,
					 &s,
					 match_kcmd_version,
					 NULL,
					 server,
					 KRB5_RECVAUTH_IGNORE_VERSION,
					 NULL,
					 &ticket);
    krb5_free_principal (context, server);
    if (status)
	syslog_and_die ("krb5_recvauth: %s",
			krb5_get_err_text(context, status));

    *server_username = read_str (s, USERNAME_SZ, "remote username");
    *cmd = read_str (s, ARG_MAX + 1, "command");
    *client_username = read_str (s, ARG_MAX + 1, "local username");

    if(protocol_version == 2) {
	status = krb5_auth_con_getremotesubkey(context, auth_context,
					       &keyblock);
	if(status != 0 || keyblock == NULL)
	    syslog_and_die("failed to get remote subkey");
    } else if(protocol_version == 1) {
	status = krb5_auth_con_getkey (context, auth_context, &keyblock);
	if(status != 0 || keyblock == NULL)
	    syslog_and_die("failed to get key");
    }
    if (status != 0 || keyblock == NULL)
       syslog_and_die ("krb5_auth_con_getkey: %s",
                       krb5_get_err_text(context, status));

    status = krb5_crypto_init(context, keyblock, 0, &crypto);
    if(status)
	syslog_and_die("krb5_crypto_init: %s",
		       krb5_get_err_text(context, status));


    cksum_data.length = asprintf (&str,
				  "%u:%s%s",
				  ntohs(socket_get_port (thisaddr)),
				  *cmd,
				  *server_username);
    if (str == NULL)
	syslog_and_die ("asprintf: out of memory");
    cksum_data.data = str;

    status = krb5_verify_authenticator_checksum(context,
						auth_context,
						cksum_data.data,
						cksum_data.length);

    if (status)
	syslog_and_die ("krb5_verify_authenticator_checksum: %s",
			krb5_get_err_text(context, status));

    free (cksum_data.data);

    if (strncmp (*client_username, "-u ", 3) == 0) {
	do_unique_tkfile = 1;
	memmove (*client_username, *client_username + 3,
		 strlen(*client_username) - 2);
    }

    if (strncmp (*client_username, "-U ", 3) == 0) {
	char *end, *temp_tkfile;

	do_unique_tkfile = 1;
	if (strncmp (*client_username + 3, "FILE:", 5) == 0) {
	    temp_tkfile = tkfile;
	} else {
	    strlcpy (tkfile, "FILE:", sizeof(tkfile));
	    temp_tkfile = tkfile + 5;
	}
	end = strchr(*client_username + 3,' ');
	if (end == NULL)
	    syslog_and_die("missing argument after -U");
	snprintf(temp_tkfile, sizeof(tkfile) - (temp_tkfile - tkfile),
		 "%.*s",
		 (int)(end - *client_username - 3),
		 *client_username + 3);
	memmove (*client_username, end + 1, strlen(end+1)+1);
    }

    kerberos_status = save_krb5_creds (s, auth_context, ticket->client);

    if(!krb5_kuserok (context,
		      ticket->client,
		      *server_username))
	fatal (s, NULL, "Permission denied.");

    if (strncmp (*cmd, "-x ", 3) == 0) {
	do_encrypt = 1;
	memmove (*cmd, *cmd + 3, strlen(*cmd) - 2);
    } else {
	if(do_encrypt)
	    fatal (s, NULL, "Encryption is required.");
	do_encrypt = 0;
    }

    {
	char *name;

	if (krb5_unparse_name (context, ticket->client, &name) == 0) {
	    char addr_str[256];

	    if (inet_ntop (thataddr->sa_family,
			   socket_get_address (thataddr),
			   addr_str, sizeof(addr_str)) == NULL)
		strlcpy (addr_str, "unknown address",
				 sizeof(addr_str));

	    syslog(LOG_INFO|LOG_AUTH,
		   "kerberos v5 shell from %s on %s as %s, cmd '%.80s'",
		   name,
		   addr_str,
		   *server_username,
		   *cmd);
	    free (name);
	}
    }

    krb5_auth_con_free(context, auth_context);

    return 0;
}
#endif /* KRB5 */

static void
rshd_loop (int from0, int to0,
	   int to1,   int from1,
	   int to2,   int from2,
	   int have_errsock)
{
    fd_set real_readset;
    int max_fd;
    int count = 2;
    char *buf;

    if(from0 >= FD_SETSIZE || from1 >= FD_SETSIZE || from2 >= FD_SETSIZE)
	errx (1, "fd too large");

#ifdef KRB5
    if(auth_method == AUTH_KRB5 && protocol_version == 2)
	init_ivecs(0, have_errsock);
#endif

    FD_ZERO(&real_readset);
    FD_SET(from0, &real_readset);
    FD_SET(from1, &real_readset);
    FD_SET(from2, &real_readset);
    max_fd = max(from0, max(from1, from2)) + 1;

    buf = malloc(max(RSHD_BUFSIZ, RSH_BUFSIZ));
    if (buf == NULL)
	syslog_and_die("out of memory");

    for (;;) {
	int ret;
	fd_set readset = real_readset;

	ret = select (max_fd, &readset, NULL, NULL, NULL);
	if (ret < 0) {
	    if (errno == EINTR)
		continue;
	    else
		syslog_and_die ("select: %s", strerror(errno));
	}
	if (FD_ISSET(from0, &readset)) {
	    ret = do_read (from0, buf, RSHD_BUFSIZ, ivec_in[0]);
	    if (ret < 0)
		syslog_and_die ("read: %s", strerror(errno));
	    else if (ret == 0) {
		close (from0);
		close (to0);
		FD_CLR(from0, &real_readset);
	    } else
		net_write (to0, buf, ret);
	}
	if (FD_ISSET(from1, &readset)) {
	    ret = read (from1, buf, RSH_BUFSIZ);
	    if (ret < 0)
		syslog_and_die ("read: %s", strerror(errno));
	    else if (ret == 0) {
		close (from1);
		close (to1);
		FD_CLR(from1, &real_readset);
		if (--count == 0)
		    exit (0);
	    } else
		do_write (to1, buf, ret, ivec_out[0]);
	}
	if (FD_ISSET(from2, &readset)) {
	    ret = read (from2, buf, RSH_BUFSIZ);
	    if (ret < 0)
		syslog_and_die ("read: %s", strerror(errno));
	    else if (ret == 0) {
		close (from2);
		close (to2);
		FD_CLR(from2, &real_readset);
		if (--count == 0)
		    exit (0);
	    } else
		do_write (to2, buf, ret, ivec_out[1]);
	}
   }
}

/*
 * Used by `setup_copier' to create some pipe-like means of
 * communcation.  Real pipes would probably be the best thing, but
 * then the shell doesn't understand it's talking to rshd.  If
 * socketpair doesn't work everywhere, some autoconf magic would have
 * to be added here.
 *
 * If it fails creating the `pipe', it aborts by calling fatal.
 */

static void
pipe_a_like (int fd[2])
{
    if (socketpair (AF_UNIX, SOCK_STREAM, 0, fd) < 0)
	fatal (STDOUT_FILENO, "socketpair", "Pipe creation failed.");
}

/*
 * Start a child process and leave the parent copying data to and from it.  */

static void
setup_copier (int have_errsock)
{
    int p0[2], p1[2], p2[2];
    pid_t pid;

    pipe_a_like(p0);
    pipe_a_like(p1);
    pipe_a_like(p2);
    pid = fork ();
    if (pid < 0)
	fatal (STDOUT_FILENO, "fork", "Could not create child process.");
    if (pid == 0) { /* child */
	close (p0[1]);
	close (p1[0]);
	close (p2[0]);
	dup2 (p0[0], STDIN_FILENO);
	dup2 (p1[1], STDOUT_FILENO);
	dup2 (p2[1], STDERR_FILENO);
	close (p0[0]);
	close (p1[1]);
	close (p2[1]);
    } else { /* parent */
	close (p0[0]);
	close (p1[1]);
	close (p2[1]);

	if (net_write (STDOUT_FILENO, "", 1) != 1)
	    fatal (STDOUT_FILENO, "net_write", "Write failure.");

	rshd_loop (STDIN_FILENO, p0[1],
	      STDOUT_FILENO, p1[0],
	      STDERR_FILENO, p2[0],
	      have_errsock);
    }
}

/*
 * Is `port' a ``reserverd'' port?
 */

static int
is_reserved(u_short port)
{
    return ntohs(port) < IPPORT_RESERVED;
}

/*
 * Set the necessary part of the environment in `env'.
 */

static void
setup_environment (char ***env, const struct passwd *pwd)
{
    int i, j, path;
    char **e;

    i = 0;
    path = 0;
    *env = NULL;

    i = read_environment(_PATH_ETC_ENVIRONMENT, env);
    e = *env;
    for (j = 0; j < i; j++) {
	if (!strncmp(e[j], "PATH=", 5)) {
	    path = 1;
	}
    }

    e = *env;
    e = realloc(e, (i + 7) * sizeof(char *));

    if (asprintf (&e[i++], "USER=%s",  pwd->pw_name) == -1)
	syslog_and_die ("asprintf: out of memory");
    if (asprintf (&e[i++], "HOME=%s",  pwd->pw_dir) == -1)
	syslog_and_die ("asprintf: out of memory");
    if (asprintf (&e[i++], "SHELL=%s", pwd->pw_shell) == -1)
	syslog_and_die ("asprintf: out of memory");
    if (! path) {
	if (asprintf (&e[i++], "PATH=%s",  _PATH_DEFPATH) == -1)
	    syslog_and_die ("asprintf: out of memory");
    }
    asprintf (&e[i++], "SSH_CLIENT=only_to_make_bash_happy");
    if (do_unique_tkfile)
	if (asprintf (&e[i++], "KRB5CCNAME=%s", tkfile) == -1)
	    syslog_and_die ("asprintf: out of memory");
    e[i++] = NULL;
    *env = e;
}

static void
doit (void)
{
    u_char buf[BUFSIZ];
    u_char *p;
    struct sockaddr_storage thisaddr_ss;
    struct sockaddr *thisaddr = (struct sockaddr *)&thisaddr_ss;
    struct sockaddr_storage thataddr_ss;
    struct sockaddr *thataddr = (struct sockaddr *)&thataddr_ss;
    struct sockaddr_storage erraddr_ss;
    struct sockaddr *erraddr = (struct sockaddr *)&erraddr_ss;
    socklen_t thisaddr_len, thataddr_len;
    int port;
    int errsock = -1;
    char *client_user = NULL, *server_user = NULL, *cmd = NULL;
    struct passwd *pwd;
    int s = STDIN_FILENO;
    char **env;
    int ret;
    char that_host[NI_MAXHOST];

    thisaddr_len = sizeof(thisaddr_ss);
    if (getsockname (s, thisaddr, &thisaddr_len) < 0)
	syslog_and_die("getsockname: %s", strerror(errno));
    thataddr_len = sizeof(thataddr_ss);
    if (getpeername (s, thataddr, &thataddr_len) < 0)
	syslog_and_die ("getpeername: %s", strerror(errno));

    /* check for V4MAPPED addresses? */

    if (do_kerberos == 0 && !is_reserved(socket_get_port(thataddr)))
	fatal(s, NULL, "Permission denied.");

    p = buf;
    port = 0;
    for(;;) {
	if (net_read (s, p, 1) != 1)
	    syslog_and_die ("reading port number: %s", strerror(errno));
	if (*p == '\0')
	    break;
	else if (isdigit(*p))
	    port = port * 10 + *p - '0';
	else
	    syslog_and_die ("non-digit in port number: %c", *p);
    }

    if (do_kerberos  == 0 && !is_reserved(htons(port)))
	fatal(s, NULL, "Permission denied.");

    if (port) {
	int priv_port = IPPORT_RESERVED - 1;

	/*
	 * There's no reason to require a ``privileged'' port number
	 * here, but for some reason the brain dead rsh clients
	 * do... :-(
	 */

	erraddr->sa_family = thataddr->sa_family;
	socket_set_address_and_port (erraddr,
				     socket_get_address (thataddr),
				     htons(port));

	/*
	 * we only do reserved port for IPv4
	 */

	if (erraddr->sa_family == AF_INET)
	    errsock = rresvport (&priv_port);
	else
	    errsock = socket (erraddr->sa_family, SOCK_STREAM, 0);
	if (errsock < 0)
	    syslog_and_die ("socket: %s", strerror(errno));
	if (connect (errsock,
		     erraddr,
		     socket_sockaddr_size (erraddr)) < 0) {
	    syslog (LOG_WARNING, "connect: %s", strerror(errno));
	    close (errsock);
	}
    }

    if(do_kerberos) {
	if (net_read (s, buf, 4) != 4)
	    syslog_and_die ("reading auth info: %s", strerror(errno));

#ifdef KRB5
	    if((do_kerberos & DO_KRB5) &&
	       recv_krb5_auth (s, buf, thisaddr, thataddr,
			       &client_user,
			       &server_user,
			       &cmd) == 0)
		auth_method = AUTH_KRB5;
	    else
#endif /* KRB5 */
		syslog_and_die ("unrecognized auth protocol: %x %x %x %x",
				buf[0], buf[1], buf[2], buf[3]);
    } else {
	if(recv_bsd_auth (s, buf,
			  (struct sockaddr_in *)thisaddr,
			  (struct sockaddr_in *)thataddr,
			  &client_user,
			  &server_user,
			  &cmd) == 0) {
	    auth_method = AUTH_BROKEN;
	    if(do_vacuous) {
		printf("Remote host requires Kerberos authentication\n");
		exit(0);
	    }
	} else
	    syslog_and_die("recv_bsd_auth failed");
    }

    if (client_user == NULL || server_user == NULL || cmd == NULL)
	syslog_and_die("mising client/server/cmd");

    pwd = getpwnam (server_user);
    if (pwd == NULL)
	fatal (s, NULL, "Login incorrect.");

    if (*pwd->pw_shell == '\0')
	pwd->pw_shell = _PATH_BSHELL;

    if (pwd->pw_uid != 0 && access (_PATH_NOLOGIN, F_OK) == 0)
	fatal (s, NULL, "Login disabled.");


    ret = getnameinfo_verified (thataddr, thataddr_len,
				that_host, sizeof(that_host),
				NULL, 0, 0);
    if (ret)
	fatal (s, NULL, "getnameinfo: %s", gai_strerror(ret));

    if (login_access(pwd, that_host) == 0) {
	syslog(LOG_NOTICE, "Kerberos rsh denied to %s from %s",
	       server_user, that_host);
	fatal(s, NULL, "Permission denied.");
    }

#ifdef HAVE_GETSPNAM
    {
	struct spwd *sp;
	long    today;

	sp = getspnam(server_user);
	if (sp != NULL) {
	    today = time(0)/(24L * 60 * 60);
	    if (sp->sp_expire > 0)
		if (today > sp->sp_expire)
		    fatal(s, NULL, "Account has expired.");
	}
    }
#endif


#ifdef HAVE_SETLOGIN
    if (setlogin(pwd->pw_name) < 0)
	syslog(LOG_ERR, "setlogin() failed: %s", strerror(errno));
#endif

#ifdef HAVE_SETPCRED
    if (setpcred (pwd->pw_name, NULL) == -1)
	syslog(LOG_ERR, "setpcred() failure: %s", strerror(errno));
#endif /* HAVE_SETPCRED */

    /* Apply limits if not root */
    if(pwd->pw_uid != 0) {
	 const char *file = _PATH_LIMITS_CONF;
	 read_limits_conf(file, pwd);
    }

    if (initgroups (pwd->pw_name, pwd->pw_gid) < 0)
	fatal (s, "initgroups", "Login incorrect.");

    if (setgid(pwd->pw_gid) < 0)
	fatal (s, "setgid", "Login incorrect.");

    if (setuid (pwd->pw_uid) < 0)
	fatal (s, "setuid", "Login incorrect.");

    if (chdir (pwd->pw_dir) < 0)
	fatal (s, "chdir", "Remote directory.");

    if (errsock >= 0) {
	if (dup2 (errsock, STDERR_FILENO) < 0)
	    fatal (s, "dup2", "Cannot dup stderr.");
	close (errsock);
    } else {
	if (dup2 (STDOUT_FILENO, STDERR_FILENO) < 0)
	    fatal (s, "dup2", "Cannot dup stderr.");
    }

#ifdef KRB5
    {
	int fd;

	if (!do_unique_tkfile)
	    snprintf(tkfile,sizeof(tkfile),"FILE:/tmp/krb5cc_%lu",
		     (unsigned long)pwd->pw_uid);
	else if (*tkfile=='\0') {
	    snprintf(tkfile,sizeof(tkfile),"FILE:/tmp/krb5cc_XXXXXX");
	    fd = mkstemp(tkfile+5);
	    close(fd);
	    unlink(tkfile+5);
	}

	if (kerberos_status)
	    krb5_start_session();
    }
#endif

    setup_environment (&env, pwd);

    if (do_encrypt) {
	setup_copier (errsock >= 0);
    } else {
	if (net_write (s, "", 1) != 1)
	    fatal (s, "net_write", "write failed");
    }

#if defined(KRB5)
    if(k_hasafs()) {
	char cell[64];

	if(do_newpag)
	    k_setpag();

	/* XXX */
       if (kerberos_status) {
	   krb5_ccache ccache;
	   krb5_error_code status;

	   status = krb5_cc_resolve (context, tkfile, &ccache);
	   if (!status) {
	       if (k_afs_cell_of_file (pwd->pw_dir, cell, sizeof(cell)) == 0)
		   krb5_afslog_uid_home(context, ccache, cell, NULL,
					pwd->pw_uid, pwd->pw_dir);
	       krb5_afslog_uid_home(context, ccache, NULL, NULL,
				    pwd->pw_uid, pwd->pw_dir);
	       krb5_cc_close (context, ccache);
	   }
       }
    }
#endif /* KRB5 */
    execle (pwd->pw_shell, pwd->pw_shell, "-c", cmd, NULL, env);
    err(1, "exec %s", pwd->pw_shell);
}

struct getargs args[] = {
    { NULL,		'a',	arg_flag,	&do_addr_verify },
    { "keepalive",	'n',	arg_negative_flag,	&do_keepalive },
    { "inetd",		'i',	arg_negative_flag,	&do_inetd,
      "Not started from inetd" },
#if defined(KRB5)
    { "kerberos",	'k',	arg_flag,	&do_kerberos,
      "Implement kerberised services" },
    { "encrypt",	'x',	arg_flag,		&do_encrypt,
      "Implement encrypted service" },
#endif
    { "rhosts",		'l',	arg_negative_flag, &do_rhosts,
      "Don't check users .rhosts" },
    { "port",		'p',	arg_string,	&port_str,	"Use this port",
      "port" },
    { "vacuous",	'v',	arg_flag, &do_vacuous,
      "Don't accept non-kerberised connections" },
#if defined(KRB5)
    { NULL,		'P',	arg_negative_flag, &do_newpag,
      "Don't put process in new PAG" },
#endif
    /* compatibility flag: */
    { NULL,		'L',	arg_flag, &do_log },
    { "version",	0, 	arg_flag,		&do_version },
    { "help",		0, 	arg_flag,		&do_help }
};

static void
usage (int ret)
{
    if(isatty(STDIN_FILENO))
	arg_printusage (args,
			sizeof(args) / sizeof(args[0]),
			NULL,
			"");
    else
	syslog (LOG_ERR, "Usage: %s [-ikxlvPL] [-p port]", getprogname());
    exit (ret);
}


int
main(int argc, char **argv)
{
    int optind = 0;
    int on = 1;

    setprogname (argv[0]);
    roken_openlog ("rshd", LOG_ODELAY | LOG_PID, LOG_AUTH);

    if (getarg(args, sizeof(args) / sizeof(args[0]), argc, argv,
	       &optind))
	usage(1);

    if(do_help)
	usage (0);

    if (do_version) {
	print_version(NULL);
	exit(0);
    }

#if defined(KRB5)
    if (do_encrypt)
	do_kerberos = 1;

    if(do_kerberos)
	do_kerberos = DO_KRB5;
#endif

#ifdef KRB5
    if((do_kerberos & DO_KRB5) && krb5_init_context (&context) != 0)
	do_kerberos &= ~DO_KRB5;
#endif

    if (!do_inetd) {
	int error;
	struct addrinfo *ai = NULL, hints;
	char portstr[NI_MAXSERV];

	memset (&hints, 0, sizeof(hints));
	hints.ai_flags    = AI_PASSIVE;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_family   = PF_UNSPEC;

	if(port_str != NULL) {
	    error = getaddrinfo (NULL, port_str, &hints, &ai);
	    if (error)
		errx (1, "getaddrinfo: %s", gai_strerror (error));
	}
	if (ai == NULL) {
#if defined(KRB5)
	    if (do_kerberos) {
		if (do_encrypt) {
		    error = getaddrinfo(NULL, "ekshell", &hints, &ai);
		    if(error == EAI_NONAME) {
			snprintf(portstr, sizeof(portstr), "%d", 545);
			error = getaddrinfo(NULL, portstr, &hints, &ai);
		    }
		    if(error)
			errx (1, "getaddrinfo: %s", gai_strerror (error));
		} else {
		    error = getaddrinfo(NULL, "kshell", &hints, &ai);
		    if(error == EAI_NONAME) {
			snprintf(portstr, sizeof(portstr), "%d", 544);
			error = getaddrinfo(NULL, portstr, &hints, &ai);
		    }
		    if(error)
			errx (1, "getaddrinfo: %s", gai_strerror (error));
		}
	    } else
#endif
		{
		    error = getaddrinfo(NULL, "shell", &hints, &ai);
		    if(error == EAI_NONAME) {
			snprintf(portstr, sizeof(portstr), "%d", 514);
			error = getaddrinfo(NULL, portstr, &hints, &ai);
		    }
		    if(error)
			errx (1, "getaddrinfo: %s", gai_strerror (error));
		}
	}
	mini_inetd_addrinfo (ai, NULL);
	freeaddrinfo(ai);
    }

    if (do_keepalive &&
	setsockopt(0, SOL_SOCKET, SO_KEEPALIVE, (char *)&on,
		   sizeof(on)) < 0)
	syslog(LOG_WARNING, "setsockopt (SO_KEEPALIVE): %s", strerror(errno));

    /* set SO_LINGER? */

    signal (SIGPIPE, SIG_IGN);

    doit ();
    return 0;
}
