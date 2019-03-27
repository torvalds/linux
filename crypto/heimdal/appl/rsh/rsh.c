/*
 * Copyright (c) 1997 - 2004 Kungliga Tekniska Högskolan
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
RCSID("$Id$");

enum auth_method auth_method;
#if defined(KRB5)
int do_encrypt       = -1;
#endif
#ifdef KRB5
int do_unique_tkfile = 0;
char *unique_tkfile  = NULL;
char tkfile[MAXPATHLEN];
int do_forward       = -1;
int do_forwardable   = -1;
krb5_context context;
krb5_keyblock *keyblock;
krb5_crypto crypto;
#endif
int sock_debug	     = 0;

#ifdef KRB5
static int use_v5 = -1;
#endif
#if defined(KRB5)
static int use_only_broken = 0;
#else
static int use_only_broken = 1;
#endif
static int use_broken = 1;
static char *port_str;
static const char *user;
static int do_version;
static int do_help;
static int do_errsock = 1;
#ifdef KRB5
static char *protocol_version_str;
static int protocol_version = 2;
#endif

/*
 *
 */

static int input = 1;		/* Read from stdin */

static int
rsh_loop (int s, int errsock)
{
    fd_set real_readset;
    int count = 1;

#ifdef KRB5
    if(auth_method == AUTH_KRB5 && protocol_version == 2)
	init_ivecs(1, errsock != -1);
#endif

    if (s >= FD_SETSIZE || (errsock != -1 && errsock >= FD_SETSIZE))
	errx (1, "fd too large");

    FD_ZERO(&real_readset);
    FD_SET(s, &real_readset);
    if (errsock != -1) {
	FD_SET(errsock, &real_readset);
	++count;
    }
    if(input)
	FD_SET(STDIN_FILENO, &real_readset);

    for (;;) {
	int ret;
	fd_set readset;
	char buf[RSH_BUFSIZ];

	readset = real_readset;
	ret = select (max(s, errsock) + 1, &readset, NULL, NULL, NULL);
	if (ret < 0) {
	    if (errno == EINTR)
		continue;
	    else
		err (1, "select");
	}
	if (FD_ISSET(s, &readset)) {
	    ret = do_read (s, buf, sizeof(buf), ivec_in[0]);
	    if (ret < 0)
		err (1, "read");
	    else if (ret == 0) {
		close (s);
		FD_CLR(s, &real_readset);
		if (--count == 0)
		    return 0;
	    } else
		net_write (STDOUT_FILENO, buf, ret);
	}
	if (errsock != -1 && FD_ISSET(errsock, &readset)) {
	    ret = do_read (errsock, buf, sizeof(buf), ivec_in[1]);
	    if (ret < 0)
		err (1, "read");
	    else if (ret == 0) {
		close (errsock);
		FD_CLR(errsock, &real_readset);
		if (--count == 0)
		    return 0;
	    } else
		net_write (STDERR_FILENO, buf, ret);
	}
	if (FD_ISSET(STDIN_FILENO, &readset)) {
	    ret = read (STDIN_FILENO, buf, sizeof(buf));
	    if (ret < 0)
		err (1, "read");
	    else if (ret == 0) {
		close (STDIN_FILENO);
		FD_CLR(STDIN_FILENO, &real_readset);
		shutdown (s, SHUT_WR);
	    } else
		do_write (s, buf, ret, ivec_out[0]);
	}
    }
}

#ifdef KRB5
/*
 * Send forward information on `s' for host `hostname', them being
 * forwardable themselves if `forwardable'
 */

static int
krb5_forward_cred (krb5_auth_context auth_context,
		   int s,
		   const char *hostname,
		   int forwardable)
{
    krb5_error_code ret;
    krb5_ccache     ccache;
    krb5_creds      creds;
    krb5_kdc_flags  flags;
    krb5_data       out_data;
    krb5_principal  principal;

    memset (&creds, 0, sizeof(creds));

    ret = krb5_cc_default (context, &ccache);
    if (ret) {
	warnx ("could not forward creds: krb5_cc_default: %s",
	       krb5_get_err_text (context, ret));
	return 1;
    }

    ret = krb5_cc_get_principal (context, ccache, &principal);
    if (ret) {
	warnx ("could not forward creds: krb5_cc_get_principal: %s",
	       krb5_get_err_text (context, ret));
	return 1;
    }

    creds.client = principal;

    ret = krb5_make_principal(context,
			      &creds.server,
			      principal->realm,
			      "krbtgt",
			      principal->realm,
			      NULL);

    if (ret) {
	warnx ("could not forward creds: krb5_make_principal: %s",
	       krb5_get_err_text (context, ret));
	return 1;
    }

    creds.times.endtime = 0;

    flags.i = 0;
    flags.b.forwarded   = 1;
    flags.b.forwardable = forwardable;

    ret = krb5_get_forwarded_creds (context,
				    auth_context,
				    ccache,
				    flags.i,
				    hostname,
				    &creds,
				    &out_data);
    if (ret) {
	warnx ("could not forward creds: krb5_get_forwarded_creds: %s",
	       krb5_get_err_text (context, ret));
	return 1;
    }

    ret = krb5_write_message (context,
			      (void *)&s,
			      &out_data);
    krb5_data_free (&out_data);

    if (ret)
	warnx ("could not forward creds: krb5_write_message: %s",
	       krb5_get_err_text (context, ret));
    return 0;
}

static int sendauth_version_error;

static int
send_krb5_auth(int s,
	       struct sockaddr *thisaddr,
	       struct sockaddr *thataddr,
	       const char *hostname,
	       const char *remote_user,
	       const char *local_user,
	       size_t cmd_len,
	       const char *cmd)
{
    krb5_principal server;
    krb5_data cksum_data;
    int status;
    size_t len;
    krb5_auth_context auth_context = NULL;
    const char *protocol_string = NULL;
    krb5_flags ap_opts;
    char *str;

    status = krb5_sname_to_principal(context,
				     hostname,
				     "host",
				     KRB5_NT_SRV_HST,
				     &server);
    if (status) {
	warnx ("%s: %s", hostname, krb5_get_err_text(context, status));
	return 1;
    }

    if(do_encrypt == -1) {
	krb5_appdefault_boolean(context, NULL,
				krb5_principal_get_realm(context, server),
				"encrypt",
				FALSE,
				&do_encrypt);
    }

    cksum_data.length = asprintf (&str,
				  "%u:%s%s%s",
				  ntohs(socket_get_port(thataddr)),
				  do_encrypt ? "-x " : "",
				  cmd,
				  remote_user);
    if (str == NULL) {
	warnx ("%s: failed to allocate command", hostname);
	return 1;
    }
    cksum_data.data = str;

    ap_opts = 0;

    if(do_encrypt)
	ap_opts |= AP_OPTS_MUTUAL_REQUIRED;

    switch(protocol_version) {
    case 2:
	ap_opts |= AP_OPTS_USE_SUBKEY;
	protocol_string = KCMD_NEW_VERSION;
	break;
    case 1:
	protocol_string = KCMD_OLD_VERSION;
	key_usage = KRB5_KU_OTHER_ENCRYPTED;
	break;
    default:
	abort();
    }

    status = krb5_sendauth (context,
			    &auth_context,
			    &s,
			    protocol_string,
			    NULL,
			    server,
			    ap_opts,
			    &cksum_data,
			    NULL,
			    NULL,
			    NULL,
			    NULL,
			    NULL);

    /* do this while we have a principal */
    if(do_forward == -1 || do_forwardable == -1) {
	krb5_const_realm realm = krb5_principal_get_realm(context, server);
	if (do_forwardable == -1)
	    krb5_appdefault_boolean(context, NULL, realm,
				    "forwardable", FALSE,
				    &do_forwardable);
	if (do_forward == -1)
	    krb5_appdefault_boolean(context, NULL, realm,
				    "forward", FALSE,
				    &do_forward);
    }

    krb5_free_principal(context, server);
    krb5_data_free(&cksum_data);

    if (status) {
	if(status == KRB5_SENDAUTH_REJECTED &&
	   protocol_version == 2 && protocol_version_str == NULL)
	    sendauth_version_error = 1;
	else
	    krb5_warn(context, status, "%s", hostname);
	return 1;
    }

    status = krb5_auth_con_getlocalsubkey (context, auth_context, &keyblock);
    if(keyblock == NULL)
	status = krb5_auth_con_getkey (context, auth_context, &keyblock);
    if (status) {
	warnx ("krb5_auth_con_getkey: %s", krb5_get_err_text(context, status));
	return 1;
    }

    status = krb5_auth_con_setaddrs_from_fd (context,
					     auth_context,
					     &s);
    if (status) {
        warnx("krb5_auth_con_setaddrs_from_fd: %s",
	      krb5_get_err_text(context, status));
        return(1);
    }

    status = krb5_crypto_init(context, keyblock, 0, &crypto);
    if(status) {
	warnx ("krb5_crypto_init: %s", krb5_get_err_text(context, status));
	return 1;
    }

    len = strlen(remote_user) + 1;
    if (net_write (s, remote_user, len) != len) {
	warn ("write");
	return 1;
    }
    if (do_encrypt && net_write (s, "-x ", 3) != 3) {
	warn ("write");
	return 1;
    }
    if (net_write (s, cmd, cmd_len) != cmd_len) {
	warn ("write");
	return 1;
    }

    if (do_unique_tkfile) {
	if (net_write (s, tkfile, strlen(tkfile)) != strlen(tkfile)) {
	    warn ("write");
	    return 1;
	}
    }
    len = strlen(local_user) + 1;
    if (net_write (s, local_user, len) != len) {
	warn ("write");
	return 1;
    }

    if (!do_forward
	|| krb5_forward_cred (auth_context, s, hostname, do_forwardable)) {
	/* Empty forwarding info */

	u_char zero[4] = {0, 0, 0, 0};
	write (s, &zero, 4);
    }
    krb5_auth_con_free (context, auth_context);
    return 0;
}

#endif /* KRB5 */

static int
send_broken_auth(int s,
		 struct sockaddr *thisaddr,
		 struct sockaddr *thataddr,
		 const char *hostname,
		 const char *remote_user,
		 const char *local_user,
		 size_t cmd_len,
		 const char *cmd)
{
    size_t len;

    len = strlen(local_user) + 1;
    if (net_write (s, local_user, len) != len) {
	warn ("write");
	return 1;
    }
    len = strlen(remote_user) + 1;
    if (net_write (s, remote_user, len) != len) {
	warn ("write");
	return 1;
    }
    if (net_write (s, cmd, cmd_len) != cmd_len) {
	warn ("write");
	return 1;
    }
    return 0;
}

static int
proto (int s, int errsock,
       const char *hostname, const char *local_user, const char *remote_user,
       const char *cmd, size_t cmd_len,
       int (*auth_func)(int s,
			struct sockaddr *this, struct sockaddr *that,
			const char *hostname, const char *remote_user,
			const char *local_user, size_t cmd_len,
			const char *cmd))
{
    int errsock2;
    char buf[BUFSIZ];
    char *p;
    size_t len;
    char reply;
    struct sockaddr_storage thisaddr_ss;
    struct sockaddr *thisaddr = (struct sockaddr *)&thisaddr_ss;
    struct sockaddr_storage thataddr_ss;
    struct sockaddr *thataddr = (struct sockaddr *)&thataddr_ss;
    struct sockaddr_storage erraddr_ss;
    struct sockaddr *erraddr = (struct sockaddr *)&erraddr_ss;
    socklen_t addrlen;
    int ret;

    addrlen = sizeof(thisaddr_ss);
    if (getsockname (s, thisaddr, &addrlen) < 0) {
	warn ("getsockname(%s)", hostname);
	return 1;
    }
    addrlen = sizeof(thataddr_ss);
    if (getpeername (s, thataddr, &addrlen) < 0) {
	warn ("getpeername(%s)", hostname);
	return 1;
    }

    if (errsock != -1) {

	addrlen = sizeof(erraddr_ss);
	if (getsockname (errsock, erraddr, &addrlen) < 0) {
	    warn ("getsockname");
	    return 1;
	}

	if (listen (errsock, 1) < 0) {
	    warn ("listen");
	    return 1;
	}

	p = buf;
	snprintf (p, sizeof(buf), "%u",
		  ntohs(socket_get_port(erraddr)));
	len = strlen(buf) + 1;
	if(net_write (s, buf, len) != len) {
	    warn ("write");
	    close (errsock);
	    return 1;
	}


	for (;;) {
	    fd_set fdset;

	    if (errsock >= FD_SETSIZE || s >= FD_SETSIZE)
		errx (1, "fd too large");

	    FD_ZERO(&fdset);
	    FD_SET(errsock, &fdset);
	    FD_SET(s, &fdset);

	    ret = select (max(errsock, s) + 1, &fdset, NULL, NULL, NULL);
	    if (ret < 0) {
		if (errno == EINTR)
		    continue;
		warn ("select");
		close (errsock);
		return 1;
	    }
	    if (FD_ISSET(errsock, &fdset)) {
		errsock2 = accept (errsock, NULL, NULL);
		close (errsock);
		if (errsock2 < 0) {
		    warn ("accept");
		    return 1;
		}
		break;
	    }

	    /*
	     * there should not arrive any data on this fd so if it's
	     * readable it probably indicates that the other side when
	     * away.
	     */

	    if (FD_ISSET(s, &fdset)) {
		warnx ("socket closed");
		close (errsock);
		errsock2 = -1;
		break;
	    }
	}
    } else {
	if (net_write (s, "0", 2) != 2) {
	    warn ("write");
	    return 1;
	}
	errsock2 = -1;
    }

    if ((*auth_func)(s, thisaddr, thataddr, hostname,
		     remote_user, local_user,
		     cmd_len, cmd)) {
	close (errsock2);
	return 1;
    }

    ret = net_read (s, &reply, 1);
    if (ret < 0) {
	warn ("read");
	close (errsock2);
	return 1;
    } else if (ret == 0) {
	warnx ("unexpected EOF from %s", hostname);
	close (errsock2);
	return 1;
    }
    if (reply != 0) {

	warnx ("Error from rshd at %s:", hostname);

	while ((ret = read (s, buf, sizeof(buf))) > 0)
	    write (STDOUT_FILENO, buf, ret);
        write (STDOUT_FILENO,"\n",1);
	close (errsock2);
	return 1;
    }

    if (sock_debug) {
	int one = 1;
	if (setsockopt(s, SOL_SOCKET, SO_DEBUG, (void *)&one, sizeof(one)) < 0)
	    warn("setsockopt remote");
	if (errsock2 != -1 &&
	    setsockopt(errsock2, SOL_SOCKET, SO_DEBUG,
		       (void *)&one, sizeof(one)) < 0)
	    warn("setsockopt stderr");
    }

    return rsh_loop (s, errsock2);
}

/*
 * Return in `res' a copy of the concatenation of `argc, argv' into
 * malloced space.  */

static size_t
construct_command (char **res, int argc, char **argv)
{
    int i;
    size_t len = 0;
    char *tmp;

    for (i = 0; i < argc; ++i)
	len += strlen(argv[i]) + 1;
    len = max (1, len);
    tmp = malloc (len);
    if (tmp == NULL)
	errx (1, "malloc %lu failed", (unsigned long)len);

    *tmp = '\0';
    for (i = 0; i < argc - 1; ++i) {
	strlcat (tmp, argv[i], len);
	strlcat (tmp, " ", len);
    }
    if (argc > 0)
	strlcat (tmp, argv[argc-1], len);
    *res = tmp;
    return len;
}

static char *
print_addr (const struct sockaddr *sa)
{
    char addr_str[256];
    char *res;
    const char *as = NULL;

    if(sa->sa_family == AF_INET)
	as = inet_ntop (sa->sa_family, &((struct sockaddr_in*)sa)->sin_addr,
			addr_str, sizeof(addr_str));
#ifdef HAVE_INET6
    else if(sa->sa_family == AF_INET6)
	as = inet_ntop (sa->sa_family, &((struct sockaddr_in6*)sa)->sin6_addr,
			addr_str, sizeof(addr_str));
#endif
    if(as == NULL)
	return NULL;
    res = strdup(as);
    if (res == NULL)
	errx (1, "malloc: out of memory");
    return res;
}

static int
doit_broken (int argc,
	     char **argv,
	     int hostindex,
	     struct addrinfo *ai,
	     const char *remote_user,
	     const char *local_user,
	     int priv_socket1,
	     int priv_socket2,
	     const char *cmd,
	     size_t cmd_len)
{
    struct addrinfo *a;

    if (connect (priv_socket1, ai->ai_addr, ai->ai_addrlen) < 0) {
	int save_errno = errno;

	close(priv_socket1);
	close(priv_socket2);

	for (a = ai->ai_next; a != NULL; a = a->ai_next) {
	    pid_t pid;
	    char *adr = print_addr(a->ai_addr);
	    if(adr == NULL)
		continue;

	    pid = fork();
	    if (pid < 0)
		err (1, "fork");
	    else if(pid == 0) {
		char **new_argv;
		int i = 0;

		new_argv = malloc((argc + 2) * sizeof(*new_argv));
		if (new_argv == NULL)
		    errx (1, "malloc: out of memory");
		new_argv[i] = argv[i];
		++i;
		if (hostindex == i)
		    new_argv[i++] = adr;
		new_argv[i++] = "-K";
		for(; i <= argc; ++i)
		    new_argv[i] = argv[i - 1];
		if (hostindex > 1)
		    new_argv[hostindex + 1] = adr;
		new_argv[argc + 1] = NULL;
		execv(PATH_RSH, new_argv);
		err(1, "execv(%s)", PATH_RSH);
	    } else {
		int status;
		free(adr);

		while(waitpid(pid, &status, 0) < 0)
		    ;
		if(WIFEXITED(status) && WEXITSTATUS(status) == 0)
		    return 0;
	    }
	}
	errno = save_errno;
	warn("%s", argv[hostindex]);
	return 1;
    } else {
	int ret;

	ret = proto (priv_socket1, priv_socket2,
		     argv[hostindex],
		     local_user, remote_user,
		     cmd, cmd_len,
		     send_broken_auth);
	return ret;
    }
}

#if defined(KRB5)
static int
doit (const char *hostname,
      struct addrinfo *ai,
      const char *remote_user,
      const char *local_user,
      const char *cmd,
      size_t cmd_len,
      int (*auth_func)(int s,
		       struct sockaddr *this, struct sockaddr *that,
		       const char *hostname, const char *remote_user,
		       const char *local_user, size_t cmd_len,
		       const char *cmd))
{
    int error;
    struct addrinfo *a;
    int socketfailed = 1;
    int ret;

    for (a = ai; a != NULL; a = a->ai_next) {
	int s;
	int errsock;

	s = socket (a->ai_family, a->ai_socktype, a->ai_protocol);
	if (s < 0)
	    continue;
	socketfailed = 0;
	if (connect (s, a->ai_addr, a->ai_addrlen) < 0) {
	    char addr[128];
	    if(getnameinfo(a->ai_addr, a->ai_addrlen,
			   addr, sizeof(addr), NULL, 0, NI_NUMERICHOST) == 0)
		warn ("connect(%s [%s])", hostname, addr);
	    else
		warn ("connect(%s)", hostname);
	    close (s);
	    continue;
	}
	if (do_errsock) {
	    struct addrinfo *ea, *eai;
	    struct addrinfo hints;

	    memset (&hints, 0, sizeof(hints));
	    hints.ai_socktype = a->ai_socktype;
	    hints.ai_protocol = a->ai_protocol;
	    hints.ai_family   = a->ai_family;
	    hints.ai_flags    = AI_PASSIVE;

	    errsock = -1;

	    error = getaddrinfo (NULL, "0", &hints, &eai);
	    if (error)
		errx (1, "getaddrinfo: %s", gai_strerror(error));
	    for (ea = eai; ea != NULL; ea = ea->ai_next) {
		errsock = socket (ea->ai_family, ea->ai_socktype,
				  ea->ai_protocol);
		if (errsock < 0)
		    continue;
		if (bind (errsock, ea->ai_addr, ea->ai_addrlen) < 0)
		    err (1, "bind");
		break;
	    }
	    if (errsock < 0)
		err (1, "socket");
	    freeaddrinfo (eai);
	} else
	    errsock = -1;

	ret = proto (s, errsock,
		     hostname,
		     local_user, remote_user,
		     cmd, cmd_len, auth_func);
	close (s);
	return ret;
    }
    if(socketfailed)
	warnx ("failed to contact %s", hostname);
    return -1;
}
#endif /* KRB5 */

struct getargs args[] = {
#ifdef KRB5
    { "krb5",	'5', arg_flag,		&use_v5,	"Use Kerberos V5" },
    { "forward", 'f', arg_flag,		&do_forward,	"Forward credentials [krb5]"},
    { "forwardable", 'F', arg_flag,	&do_forwardable,
      "Forward forwardable credentials [krb5]" },
    { NULL, 'G', arg_negative_flag,&do_forward,	"Don't forward credentials" },
    { "unique", 'u', arg_flag,	&do_unique_tkfile,
      "Use unique remote credentials cache [krb5]" },
    { "tkfile", 'U', arg_string,  &unique_tkfile,
      "Specifies remote credentials cache [krb5]" },
    { "protocol", 'P', arg_string,      &protocol_version_str,
      "Protocol version [krb5]", "protocol" },
#endif
    { "broken", 'K', arg_flag,		&use_only_broken, "Use only priv port" },
#if defined(KRB5)
    { "encrypt", 'x', arg_flag,		&do_encrypt,	"Encrypt connection" },
    { NULL, 	'z', arg_negative_flag,      &do_encrypt,
      "Don't encrypt connection", NULL },
#endif
    { NULL,	'd', arg_flag,		&sock_debug, "Enable socket debugging" },
    { "input",	'n', arg_negative_flag,	&input,		"Close stdin" },
    { "port",	'p', arg_string,	&port_str,	"Use this port",
      "port" },
    { "user",	'l', arg_string,	&user,		"Run as this user", "login" },
    { "stderr", 'e', arg_negative_flag, &do_errsock,	"Don't open stderr"},
#ifdef KRB5
#endif
    { "version", 0,  arg_flag,		&do_version,	NULL },
    { "help",	 0,  arg_flag,		&do_help,	NULL }
};

static void
usage (int ret)
{
    arg_printusage (args,
		    sizeof(args) / sizeof(args[0]),
		    NULL,
		    "[login@]host [command]");
    exit (ret);
}

/*
 *
 */

int
main(int argc, char **argv)
{
    int priv_port1, priv_port2;
    int priv_socket1, priv_socket2;
    int argindex = 0;
    int error;
    struct addrinfo hints, *ai;
    int ret = 1;
    char *cmd;
    char *tmp;
    size_t cmd_len;
    const char *local_user;
    char *host = NULL;
    int host_index = -1;
#ifdef KRB5
    int status;
#endif
    uid_t uid;

    priv_port1 = priv_port2 = IPPORT_RESERVED-1;
    priv_socket1 = rresvport(&priv_port1);
    priv_socket2 = rresvport(&priv_port2);
    uid = getuid ();
    if (setuid (uid) || (uid != 0 && setuid(0) == 0))
	err (1, "setuid");

    setprogname (argv[0]);

    if (argc >= 2 && argv[1][0] != '-') {
	host = argv[host_index = 1];
	argindex = 1;
    }

    if (getarg (args, sizeof(args) / sizeof(args[0]), argc, argv,
		&argindex))
	usage (1);

    if (do_help)
	usage (0);

    if (do_version) {
	print_version (NULL);
	return 0;
    }

#ifdef KRB5
    if(protocol_version_str != NULL) {
	if(strcasecmp(protocol_version_str, "N") == 0)
	    protocol_version = 2;
	else if(strcasecmp(protocol_version_str, "O") == 0)
	    protocol_version = 1;
	else {
	    char *end;
	    int v;
	    v = strtol(protocol_version_str, &end, 0);
	    if(*end != '\0' || (v != 1 && v != 2)) {
		errx(1, "unknown protocol version \"%s\"",
		     protocol_version_str);
	    }
	    protocol_version = v;
	}
    }

    status = krb5_init_context (&context);
    if (status) {
	if(use_v5 == 1)
	    errx(1, "krb5_init_context failed: %d", status);
	else
	    use_v5 = 0;
    }

    /* request for forwardable on the command line means we should
       also forward */
    if (do_forwardable == 1)
	do_forward = 1;

#endif

    if (use_only_broken) {
#ifdef KRB5
	use_v5 = 0;
#endif
    }

    if(priv_socket1 < 0) {
	if (use_only_broken)
	    errx (1, "unable to bind reserved port: is rsh setuid root?");
	use_broken = 0;
    }

#if defined(KRB5)
    if (do_encrypt == 1 && use_only_broken)
	errx (1, "encryption not supported with old style authentication");
#endif



#ifdef KRB5
    if (do_unique_tkfile && unique_tkfile != NULL)
	errx (1, "Only one of -u and -U allowed.");

    if (do_unique_tkfile)
	strlcpy(tkfile,"-u ", sizeof(tkfile));
    else if (unique_tkfile != NULL) {
	if (strchr(unique_tkfile,' ') != NULL) {
	    warnx("Space is not allowed in tkfilename");
	    usage(1);
	}
	do_unique_tkfile = 1;
	snprintf (tkfile, sizeof(tkfile), "-U %s ", unique_tkfile);
    }
#endif

    if (host == NULL) {
	if (argc - argindex < 1)
	    usage (1);
	else
	    host = argv[host_index = argindex++];
    }

    if((tmp = strchr(host, '@')) != NULL) {
	*tmp++ = '\0';
	user = host;
	host = tmp;
    }

    if (argindex == argc) {
	close (priv_socket1);
	close (priv_socket2);
	argv[0] = "rlogin";
	execvp ("rlogin", argv);
	err (1, "execvp rlogin");
    }

    local_user = get_default_username ();
    if (local_user == NULL)
	errx (1, "who are you?");

    if (user == NULL)
	user = local_user;

    cmd_len = construct_command(&cmd, argc - argindex, argv + argindex);

    /*
     * Try all different authentication methods
     */

#ifdef KRB5
    if (ret && use_v5) {
	memset (&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	if(port_str == NULL) {
	    error = getaddrinfo(host, "kshell", &hints, &ai);
	    if(error == EAI_NONAME)
		error = getaddrinfo(host, "544", &hints, &ai);
	} else
	    error = getaddrinfo(host, port_str, &hints, &ai);

	if(error)
	    errx (1, "getaddrinfo: %s", gai_strerror(error));

	auth_method = AUTH_KRB5;
      again:
	ret = doit (host, ai, user, local_user, cmd, cmd_len,
		    send_krb5_auth);
	if(ret != 0 && sendauth_version_error &&
	   protocol_version == 2) {
	    protocol_version = 1;
	    goto again;
	}
	freeaddrinfo(ai);
    }
#endif
    if (ret && use_broken) {
	memset (&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	if(port_str == NULL) {
	    error = getaddrinfo(host, "shell", &hints, &ai);
	    if(error == EAI_NONAME)
		error = getaddrinfo(host, "514", &hints, &ai);
	} else
	    error = getaddrinfo(host, port_str, &hints, &ai);

	if(error)
	    errx (1, "getaddrinfo: %s", gai_strerror(error));

	auth_method = AUTH_BROKEN;
	ret = doit_broken (argc, argv, host_index, ai,
			   user, local_user,
			   priv_socket1,
			   do_errsock ? priv_socket2 : -1,
			   cmd, cmd_len);
	freeaddrinfo(ai);
    }
    free(cmd);
    return ret;
}
