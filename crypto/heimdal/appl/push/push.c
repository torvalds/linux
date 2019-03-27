/*
 * Copyright (c) 1997-2004 Kungliga Tekniska Högskolan
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

#include "push_locl.h"
RCSID("$Id$");

#if defined(_AIX) && defined(STAT)
/*
 * AIX defines STAT to 1 in sys/dir.h
 */
#  undef STAT
#endif

#ifdef KRB5
static int use_v5 = -1;
static krb5_context context;
#endif

static char *port_str;
static int verbose_level;
static int do_fork;
static int do_leave;
static int do_version;
static int do_help;
static int do_from;
static int do_count;
static char *header_str;

struct getargs args[] = {
#ifdef KRB5
    { "krb5",	'5', arg_flag,		&use_v5,	"Use Kerberos V5",
      NULL },
#endif
    { "verbose",'v', arg_counter,	&verbose_level,	"Verbose",
      NULL },
    { "fork",	'f', arg_flag,		&do_fork,	"Fork deleting proc",
      NULL },
    { "leave",	'l', arg_flag,		&do_leave,	"Leave mail on server",
      NULL },
    { "port",	'p', arg_string,	&port_str,	"Use this port",
      "number-or-service" },
    { "from",	 0,  arg_flag,		&do_from,	"Behave like from",
      NULL },
    { "headers", 0,  arg_string,	&header_str,	"Headers to print", NULL },
    { "count", 'c',  arg_flag,		&do_count,	"Print number of messages", NULL},
    { "version", 0,  arg_flag,		&do_version,	"Print version",
      NULL },
    { "help",	 0,  arg_flag,		&do_help,	NULL,
      NULL }

};

static void
usage (int ret)
{
    arg_printusage (args,
		    sizeof(args) / sizeof(args[0]),
		    NULL,
		    "[[{po:username[@hostname] | hostname[:username]}] ...] "
		    "filename");
    exit (ret);
}

static int
do_connect (const char *hostname, int port, int nodelay)
{
    struct addrinfo *ai, *a;
    struct addrinfo hints;
    int error;
    int s = -1;
    char portstr[NI_MAXSERV];

    memset (&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    snprintf (portstr, sizeof(portstr), "%u", ntohs(port));

    error = getaddrinfo (hostname, portstr, &hints, &ai);
    if (error)
	errx (1, "getaddrinfo(%s): %s", hostname, gai_strerror(error));

    for (a = ai; a != NULL; a = a->ai_next) {
	s = socket (a->ai_family, a->ai_socktype, a->ai_protocol);
	if (s < 0)
	    continue;
	if (connect (s, a->ai_addr, a->ai_addrlen) < 0) {
	    warn ("connect(%s)", hostname);
 	    close (s);
 	    continue;
	}
	break;
    }
    freeaddrinfo (ai);
    if (a == NULL) {
	warnx ("failed to contact %s", hostname);
	return -1;
    }

    if(setsockopt(s, IPPROTO_TCP, TCP_NODELAY,
		  (void *)&nodelay, sizeof(nodelay)) < 0)
	err (1, "setsockopt TCP_NODELAY");
    return s;
}

typedef enum { INIT = 0, GREET, USER, PASS, STAT, RETR, TOP,
	       DELE, XDELE, QUIT} pop_state;

static char *pop_state_string[] = {
    "INIT", "GREET", "USER", "PASS", "STAT", "RETR", "TOP",
    "DELE", "XDELE", "QUIT"
};

#define PUSH_BUFSIZ 65536

#define STEP 16

struct write_state {
    struct iovec *iovecs;
    size_t niovecs, maxiovecs, allociovecs;
    int fd;
};

static void
write_state_init (struct write_state *w, int fd)
{
#ifdef UIO_MAXIOV
    w->maxiovecs = UIO_MAXIOV;
#else
    w->maxiovecs = 16;
#endif
    w->allociovecs = min(STEP, w->maxiovecs);
    w->niovecs = 0;
    w->iovecs = emalloc(w->allociovecs * sizeof(*w->iovecs));
    w->fd = fd;
}

static void
write_state_add (struct write_state *w, void *v, size_t len)
{
    if(w->niovecs == w->allociovecs) {
	if(w->niovecs == w->maxiovecs) {
	    if(writev (w->fd, w->iovecs, w->niovecs) < 0)
		err(1, "writev");
	    w->niovecs = 0;
	} else {
	    w->allociovecs = min(w->allociovecs + STEP, w->maxiovecs);
	    w->iovecs = erealloc (w->iovecs,
				  w->allociovecs * sizeof(*w->iovecs));
	}
    }
    w->iovecs[w->niovecs].iov_base = v;
    w->iovecs[w->niovecs].iov_len  = len;
    ++w->niovecs;
}

static void
write_state_flush (struct write_state *w)
{
    if (w->niovecs) {
	if (writev (w->fd, w->iovecs, w->niovecs) < 0)
	    err (1, "writev");
	w->niovecs = 0;
    }
}

static void
write_state_destroy (struct write_state *w)
{
    free (w->iovecs);
}

static int
doit(int s,
     const char *host,
     const char *user,
     const char *outfilename,
     const char *header_str,
     int leavep,
     int verbose,
     int forkp)
{
    int ret;
    char out_buf[PUSH_BUFSIZ];
    int out_len = 0;
    char *in_buf;
    size_t in_buf_size;
    size_t in_len = 0;
    char *in_ptr;
    pop_state state = INIT;
    unsigned count = 0, bytes;
    unsigned asked_for = 0, retrieved = 0, asked_deleted = 0, deleted = 0;
    unsigned sent_xdele = 0;
    int out_fd;
    char from_line[128];
    size_t from_line_length;
    time_t now;
    struct write_state write_state;
    unsigned int numheaders = 1;
    char **headers = NULL;
    int i;
    char *tmp = NULL;

    in_buf = emalloc(PUSH_BUFSIZ + 1);
    in_ptr = in_buf;
    in_buf_size = PUSH_BUFSIZ;

    if (do_from) {
	char *tmp2;

	tmp2 = tmp = estrdup(header_str);

	out_fd = -1;
	if (verbose)
	    fprintf (stderr, "%s@%s\n", user, host);
	while (*tmp != '\0') {
	    tmp = strchr(tmp, ',');
	    if (tmp == NULL)
		break;
	    tmp++;
	    numheaders++;
	}

	headers = emalloc(sizeof(char *) * (numheaders + 1));
	for (i = 0; i < numheaders; i++) {
	    headers[i] = strtok_r(tmp2, ",", &tmp2);
	}
	headers[numheaders] = NULL;
    } else {
	out_fd = open(outfilename, O_WRONLY | O_APPEND | O_CREAT, 0666);
	if (out_fd < 0)
	    err (1, "open %s", outfilename);
	if (verbose)
	    fprintf (stderr, "%s@%s -> %s\n", user, host, outfilename);
    }

    now = time(NULL);
    from_line_length = snprintf (from_line, sizeof(from_line),
				 "From %s %s", "push", ctime(&now));
    if (from_line_length < 0 || from_line_length > sizeof(from_line))
	errx (1, "snprintf failed");

    out_len = snprintf (out_buf, sizeof(out_buf),
			"USER %s\r\nPASS hej\r\nSTAT\r\n",
			user);
    if (out_len < 0 || out_len > sizeof(out_buf))
	errx (1, "snprintf failed");
    if (net_write (s, out_buf, out_len) != out_len)
	err (1, "write");
    if (verbose > 1)
	fprintf (stderr, "%s", out_buf);

    if (!do_from)
	write_state_init (&write_state, out_fd);

    while(state != QUIT) {
	fd_set readset, writeset;

	FD_ZERO(&readset);
	FD_ZERO(&writeset);
	if (s >= FD_SETSIZE)
	    errx (1, "fd too large");
	FD_SET(s,&readset);

	if (verbose > 1)
	    fprintf (stderr, "state: %s count: %d asked_for: %d "
		     "retrieved: %d asked_deleted: %d\n",
		     pop_state_string[state],
		     count, asked_for, retrieved, asked_deleted);

	if (((state == STAT || state == RETR || state == TOP)
	     && asked_for < count)
	    || (state == XDELE && !sent_xdele)
	    || (state == DELE && asked_deleted < count))
	    FD_SET(s,&writeset);
	ret = select (s + 1, &readset, &writeset, NULL, NULL);
	if (ret < 0) {
	    if (errno == EAGAIN)
		continue;
	    else
		err (1, "select");
	}

	if (FD_ISSET(s, &readset)) {
	    char *beg, *p;
	    size_t rem;
	    int blank_line = 0;

	    if(in_len >= in_buf_size) {
		char *tmp = erealloc(in_buf, in_buf_size + PUSH_BUFSIZ + 1);
		in_ptr = tmp + (in_ptr - in_buf);
		in_buf = tmp;
		in_buf_size += PUSH_BUFSIZ;
	    }

	    ret = read (s, in_ptr, in_buf_size - in_len);
	    if (ret < 0)
		err (1, "read");
	    else if (ret == 0)
		errx (1, "EOF during read");

	    in_len += ret;
	    in_ptr += ret;
	    *in_ptr = '\0';

	    beg = in_buf;
	    rem = in_len;
	    while(rem > 1
		  && (p = strstr(beg, "\r\n")) != NULL) {
		if (state == TOP) {
		    char *copy = beg;

		    for (i = 0; i < numheaders; i++) {
			size_t len;

			len = min(p - copy + 1, strlen(headers[i]));
			if (strncasecmp(copy, headers[i], len) == 0) {
			    fprintf (stdout, "%.*s\n", (int)(p - copy), copy);
			}
		    }
		    if (beg[0] == '.' && beg[1] == '\r' && beg[2] == '\n') {
			if (numheaders > 1)
			    fprintf (stdout, "\n");
			state = STAT;
			if (++retrieved == count) {
			    state = QUIT;
			    net_write (s, "QUIT\r\n", 6);
			    if (verbose > 1)
				fprintf (stderr, "QUIT\r\n");
			}
		    }
		    rem -= p - beg + 2;
		    beg = p + 2;
		} else if (state == RETR) {
		    char *copy = beg;
		    if (beg[0] == '.') {
			if (beg[1] == '\r' && beg[2] == '\n') {
			    if(!blank_line)
				write_state_add(&write_state, "\n", 1);
			    state = STAT;
			    rem -= p - beg + 2;
			    beg = p + 2;
			    if (++retrieved == count) {
				write_state_flush (&write_state);
				if (fsync (out_fd) < 0)
				    err (1, "fsync");
				close(out_fd);
				if (leavep) {
				    state = QUIT;
				    net_write (s, "QUIT\r\n", 6);
				    if (verbose > 1)
					fprintf (stderr, "QUIT\r\n");
				} else {
				    if (forkp) {
					pid_t pid;

					pid = fork();
					if (pid < 0)
					    warn ("fork");
					else if(pid != 0) {
					    if(verbose)
						fprintf (stderr,
							 "(exiting)");
					    return 0;
					}
				    }

				    state = XDELE;
				    if (verbose)
					fprintf (stderr, "deleting... ");
				}
			    }
			    continue;
			} else
			    ++copy;
		    }
		    *p = '\n';
		    if(blank_line &&
		       strncmp(copy, "From ", min(p - copy + 1, 5)) == 0)
			write_state_add(&write_state, ">", 1);
		    write_state_add(&write_state, copy, p - copy + 1);
		    blank_line = (*copy == '\n');
		    rem -= p - beg + 2;
		    beg = p + 2;
		} else if (rem >= 3 && strncmp (beg, "+OK", 3) == 0) {
		    if (state == STAT) {
			if (!do_from)
			    write_state_add(&write_state,
					    from_line, from_line_length);
			blank_line = 0;
			if (do_from)
			    state = TOP;
			else
			    state = RETR;
		    } else if (state == XDELE) {
			state = QUIT;
			net_write (s, "QUIT\r\n", 6);
			if (verbose > 1)
			    fprintf (stderr, "QUIT\r\n");
			break;
		    } else if (state == DELE) {
			if (++deleted == count) {
			    state = QUIT;
			    net_write (s, "QUIT\r\n", 6);
			    if (verbose > 1)
				fprintf (stderr, "QUIT\r\n");
			    break;
			}
		    } else if (++state == STAT) {
			if(sscanf (beg + 4, "%u %u", &count, &bytes) != 2)
			    errx(1, "Bad STAT-line: %.*s", (int)(p - beg), beg);
			if (verbose) {
			    fprintf (stderr, "%u message(s) (%u bytes). "
				     "fetching... ",
				     count, bytes);
			    if (do_from)
				fprintf (stderr, "\n");
			} else if (do_count) {
			    fprintf (stderr, "%u message(s) (%u bytes).\n",
				     count, bytes);
			}
			if (count == 0) {
			    state = QUIT;
			    net_write (s, "QUIT\r\n", 6);
			    if (verbose > 1)
				fprintf (stderr, "QUIT\r\n");
			    break;
			}
		    }

		    rem -= p - beg + 2;
		    beg = p + 2;
		} else {
		    if(state == XDELE) {
			state = DELE;
			rem -= p - beg + 2;
			beg = p + 2;
		    } else
			errx (1, "Bad response: %.*s", (int)(p - beg), beg);
		}
	    }
	    if (!do_from)
		write_state_flush (&write_state);

	    memmove (in_buf, beg, rem);
	    in_len = rem;
	    in_ptr = in_buf + rem;
	}
	if (FD_ISSET(s, &writeset)) {
	    if ((state == STAT && !do_from) || state == RETR)
		out_len = snprintf (out_buf, sizeof(out_buf),
				    "RETR %u\r\n", ++asked_for);
	    else if ((state == STAT && do_from) || state == TOP)
		out_len = snprintf (out_buf, sizeof(out_buf),
				    "TOP %u 0\r\n", ++asked_for);
	    else if(state == XDELE) {
		out_len = snprintf(out_buf, sizeof(out_buf),
				   "XDELE %u %u\r\n", 1, count);
		sent_xdele++;
	    }
	    else if(state == DELE)
		out_len = snprintf (out_buf, sizeof(out_buf),
				    "DELE %u\r\n", ++asked_deleted);
	    if (out_len < 0 || out_len > sizeof(out_buf))
		errx (1, "snprintf failed");
	    if (net_write (s, out_buf, out_len) != out_len)
		err (1, "write");
	    if (verbose > 1)
		fprintf (stderr, "%s", out_buf);
	}
    }
    if (verbose)
	fprintf (stderr, "Done\n");
    if (do_from) {
	free (tmp);
	free (headers);
    } else {
	write_state_destroy (&write_state);
    }
    return 0;
}

#ifdef KRB5
static int
do_v5 (const char *host,
       int port,
       const char *user,
       const char *filename,
       const char *header_str,
       int leavep,
       int verbose,
       int forkp)
{
    krb5_error_code ret;
    krb5_auth_context auth_context = NULL;
    krb5_principal server;
    int s;

    s = do_connect (host, port, 1);
    if (s < 0)
	return 1;

    ret = krb5_sname_to_principal (context,
				   host,
				   "pop",
				   KRB5_NT_SRV_HST,
				   &server);
    if (ret) {
	warnx ("krb5_sname_to_principal: %s",
	       krb5_get_err_text (context, ret));
	return 1;
    }

    ret = krb5_sendauth (context,
			 &auth_context,
			 &s,
			 "KPOPV1.0",
			 NULL,
			 server,
			 0,
			 NULL,
			 NULL,
			 NULL,
			 NULL,
			 NULL,
			 NULL);
    krb5_free_principal (context, server);
    if (ret) {
	warnx ("krb5_sendauth: %s",
	       krb5_get_err_text (context, ret));
	return 1;
    }
    return doit (s, host, user, filename, header_str, leavep, verbose, forkp);
}
#endif

#ifdef HESIOD

#ifdef HESIOD_INTERFACES

static char *
hesiod_get_pobox (const char **user)
{
    void *context;
    struct hesiod_postoffice *hpo;
    char *ret = NULL;

    if(hesiod_init (&context) != 0)
	err (1, "hesiod_init");

    hpo = hesiod_getmailhost (context, *user);
    if (hpo == NULL) {
	warn ("hesiod_getmailhost %s", *user);
    } else {
	if (strcasecmp(hpo->hesiod_po_type, "pop") != 0)
	    errx (1, "Unsupported po type %s", hpo->hesiod_po_type);

	ret = estrdup(hpo->hesiod_po_host);
	*user = estrdup(hpo->hesiod_po_name);
	hesiod_free_postoffice (context, hpo);
    }
    hesiod_end (context);
    return ret;
}

#else /* !HESIOD_INTERFACES */

static char *
hesiod_get_pobox (const char **user)
{
    char *ret = NULL;
    struct hes_postoffice *hpo;

    hpo = hes_getmailhost (*user);
    if (hpo == NULL) {
	warn ("hes_getmailhost %s", *user);
    } else {
	if (strcasecmp(hpo->po_type, "pop") != 0)
	    errx (1, "Unsupported po type %s", hpo->po_type);

	ret = estrdup(hpo->po_host);
	*user = estrdup(hpo->po_name);
    }
    return ret;
}

#endif /* HESIOD_INTERFACES */

#endif /* HESIOD */

static char *
get_pobox (const char **user)
{
    char *ret = NULL;

#ifdef HESIOD
    ret = hesiod_get_pobox (user);
#endif

    if (ret == NULL)
	ret = getenv("MAILHOST");
    if (ret == NULL)
	errx (1, "MAILHOST not set");
    return ret;
}

static void
parse_pobox (char *a0, const char **host, const char **user)
{
    const char *h, *u;
    char *p;
    int po = 0;

    if (a0 == NULL) {

	*user = getenv ("USERNAME");
	if (*user == NULL) {
	    struct passwd *pwd = getpwuid (getuid ());

	    if (pwd == NULL)
		errx (1, "Who are you?");
	    *user = estrdup (pwd->pw_name);
	}
	*host = get_pobox (user);
	return;
    }

    /* if the specification starts with po:, remember this information */
    if(strncmp(a0, "po:", 3) == 0) {
	a0 += 3;
	po++;
    }
    /* if there is an `@', the hostname is after it, otherwise at the
       beginning of the string */
    p = strchr(a0, '@');
    if(p != NULL) {
	*p++ = '\0';
	h = p;
    } else {
	h = a0;
    }
    /* if there is a `:', the username comes before it, otherwise at
       the beginning of the string */
    p = strchr(a0, ':');
    if(p != NULL) {
	*p++ = '\0';
	u = p;
    } else {
	u = a0;
    }
    if(h == u) {
	/* some inconsistent compatibility with various mailers */
	if(po) {
	    h = get_pobox (&u);
	} else {
	    u = get_default_username ();
	    if (u == NULL)
		errx (1, "Who are you?");
	}
    }
    *host = h;
    *user = u;
}

int
main(int argc, char **argv)
{
    int port = 0;
    int optind = 0;
    int ret = 1;
    const char *host, *user, *filename = NULL;
    char *pobox = NULL;

    setprogname (argv[0]);

#ifdef KRB5
    {
	krb5_error_code ret;

	ret = krb5_init_context (&context);
	if (ret)
	    errx (1, "krb5_init_context failed: %d", ret);
    }
#endif

    if (getarg (args, sizeof(args) / sizeof(args[0]), argc, argv,
		&optind))
	usage (1);

    argc -= optind;
    argv += optind;

    if (do_help)
	usage (0);

    if (do_version) {
	print_version(NULL);
	return 0;
    }

    if (do_from && header_str == NULL)
	header_str = "From:";
    else if (header_str != NULL)
	do_from = 1;

    if (do_from) {
	if (argc == 0)
	    pobox = NULL;
	else if (argc == 1)
	    pobox = argv[0];
	else
	    usage (1);
    } else {
	if (argc == 1) {
	    filename = argv[0];
	    pobox    = NULL;
	} else if (argc == 2) {
	    filename = argv[1];
	    pobox    = argv[0];
	} else
	    usage (1);
    }

    if (port_str) {
	struct servent *s = roken_getservbyname (port_str, "tcp");

	if (s)
	    port = s->s_port;
	else {
	    char *ptr;

	    port = strtol (port_str, &ptr, 10);
	    if (port == 0 && ptr == port_str)
		errx (1, "Bad port `%s'", port_str);
	    port = htons(port);
	}
    }
    if (port == 0) {
#ifdef KRB5
	port = krb5_getportbyname (context, "kpop", "tcp", 1109);
#else
#error must define KRB5
#endif
    }

    parse_pobox (pobox, &host, &user);

#ifdef KRB5
    if (ret && use_v5) {
	ret = do_v5 (host, port, user, filename, header_str,
		     do_leave, verbose_level, do_fork);
    }
#endif
    return ret;
}
