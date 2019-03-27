/*
 * Copyright (c) 1985, 1989, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "ftp_locl.h"
RCSID ("$Id$");

struct sockaddr_storage hisctladdr_ss;
struct sockaddr *hisctladdr = (struct sockaddr *)&hisctladdr_ss;
struct sockaddr_storage data_addr_ss;
struct sockaddr *data_addr  = (struct sockaddr *)&data_addr_ss;
struct sockaddr_storage myctladdr_ss;
struct sockaddr *myctladdr = (struct sockaddr *)&myctladdr_ss;
int data = -1;
int abrtflag = 0;
jmp_buf ptabort;
int ptabflg;
int ptflag = 0;
off_t restart_point = 0;


FILE *cin, *cout;

typedef void (*sighand) (int);

char *
hookup (const char *host, int port)
{
    static char hostnamebuf[MaxHostNameLen];
    struct addrinfo *ai, *a;
    struct addrinfo hints;
    int error;
    char portstr[NI_MAXSERV];
    socklen_t len;
    int s;

    memset (&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags    = AI_CANONNAME;

    snprintf (portstr, sizeof(portstr), "%u", ntohs(port));

    error = getaddrinfo (host, portstr, &hints, &ai);
    if (error) {
	warnx ("%s: %s", host, gai_strerror(error));
	code = -1;
	return NULL;
    }
    strlcpy (hostnamebuf, host, sizeof(hostnamebuf));
    hostname = hostnamebuf;

    s = -1;
    for (a = ai; a != NULL; a = a->ai_next) {
	s = socket (a->ai_family, a->ai_socktype, a->ai_protocol);
	if (s < 0)
	    continue;

	if (a->ai_canonname != NULL)
	    strlcpy (hostnamebuf, a->ai_canonname, sizeof(hostnamebuf));

	memcpy (hisctladdr, a->ai_addr, a->ai_addrlen);

	error = connect (s, a->ai_addr, a->ai_addrlen);
	if (error < 0) {
	    char addrstr[256];

	    if (getnameinfo (a->ai_addr, a->ai_addrlen,
			     addrstr, sizeof(addrstr),
			     NULL, 0, NI_NUMERICHOST) != 0)
		strlcpy (addrstr, "unknown address", sizeof(addrstr));

	    warn ("connect %s", addrstr);
	    close (s);
	    s = -1;
	    continue;
	}
	break;
    }
    freeaddrinfo (ai);
    if (s < 0) {
	warnx ("failed to contact %s", host);
	code = -1;
	return NULL;
    }

    len = sizeof(myctladdr_ss);
    if (getsockname (s, myctladdr, &len) < 0) {
	warn ("getsockname");
	code = -1;
	close (s);
	return NULL;
    }
#ifdef IPTOS_LOWDELAY
    socket_set_tos (s, IPTOS_LOWDELAY);
#endif
    cin = fdopen (s, "r");
    cout = fdopen (s, "w");
    if (cin == NULL || cout == NULL) {
	warnx ("fdopen failed.");
	if (cin)
	    fclose (cin);
	if (cout)
	    fclose (cout);
	code = -1;
	goto bad;
    }
    if (verbose)
	printf ("Connected to %s.\n", hostname);
    if (getreply (0) > 2) {	/* read startup message from server */
	if (cin)
	    fclose (cin);
	if (cout)
	    fclose (cout);
	code = -1;
	goto bad;
    }
#if defined(SO_OOBINLINE) && defined(HAVE_SETSOCKOPT)
    {
	int on = 1;

	if (setsockopt (s, SOL_SOCKET, SO_OOBINLINE, (char *) &on, sizeof (on))
	    < 0 && debug) {
	    warn ("setsockopt");
	}
    }
#endif				/* SO_OOBINLINE */

    return (hostname);
bad:
    close (s);
    return NULL;
}

int
login (char *host)
{
    char tmp[80];
    char defaultpass[128];
    char *userstr, *pass, *acctstr;
    char *ruserstr, *rpass, *racctstr;
    int n, aflag = 0;

    char *myname = NULL;
    struct passwd *pw = k_getpwuid(getuid());

    if (pw != NULL)
	myname = pw->pw_name;

    ruserstr = rpass = racctstr = NULL;

    if(sec_login(host))
	printf("\n*** Using plaintext user and password ***\n\n");
    else{
	printf("Authentication successful.\n\n");
    }

    if (ruserpassword (host, &ruserstr, &rpass, &racctstr) < 0) {
	code = -1;
	return (0);
    }
    userstr = ruserstr;
    pass = rpass;
    acctstr = racctstr;

    while (userstr == NULL) {
	if (myname)
	    printf ("Name (%s:%s): ", host, myname);
	else
	    printf ("Name (%s): ", host);
	*tmp = '\0';
	if (fgets (tmp, sizeof (tmp) - 1, stdin) != NULL)
	    tmp[strlen (tmp) - 1] = '\0';
	if (*tmp == '\0')
	    userstr = myname;
	else
	    userstr = tmp;
    }
    strlcpy(username, userstr, sizeof(username));
    if (ruserstr)
	free(ruserstr);

    n = command("USER %s", userstr);
    if (n == COMPLETE)
       n = command("PASS dummy"); /* DK: Compatibility with gssftp daemon */
    else if(n == CONTINUE) {
	if (pass == NULL) {
	    char prompt[128];
	    if(myname &&
	       (!strcmp(userstr, "ftp") || !strcmp(userstr, "anonymous"))) {
		snprintf(defaultpass, sizeof(defaultpass),
			 "%s@%s", myname, mydomain);
		snprintf(prompt, sizeof(prompt),
			 "Password (%s): ", defaultpass);
	    } else if (sec_complete) {
		pass = myname;
	    } else {
		*defaultpass = '\0';
		snprintf(prompt, sizeof(prompt), "Password: ");
	    }
	    if (pass == NULL) {
		pass = defaultpass;
		UI_UTIL_read_pw_string (tmp, sizeof (tmp), prompt, 0);
		if (tmp[0])
		    pass = tmp;
	    }
	}
	n = command ("PASS %s", pass);
	if (rpass)
	    free(rpass);
    }
    if (n == CONTINUE) {
	aflag++;
	UI_UTIL_read_pw_string (tmp, sizeof(tmp), "Account:", 0);
	acctstr = tmp;
	n = command ("ACCT %s", acctstr);
    }
    if (n != COMPLETE) {
	if (racctstr)
	    free(racctstr);
	warnx ("Login failed.");
	return (0);
    }
    if (!aflag && acctstr != NULL)
	command ("ACCT %s", acctstr);
    if (racctstr)
	free(racctstr);
    if (proxy)
	return (1);
    for (n = 0; n < macnum; ++n) {
	if (!strcmp("init", macros[n].mac_name)) {
	    strlcpy (line, "$init", sizeof (line));
	    makeargv();
	    domacro(margc, margv);
	    break;
	}
    }
    sec_set_protection_level ();
    return (1);
}

void
cmdabort (int sig)
{

    printf ("\n");
    fflush (stdout);
    abrtflag++;
    if (ptflag)
	longjmp (ptabort, 1);
}

int
command (char *fmt,...)
{
    va_list ap;
    int r;
    sighand oldintr;

    abrtflag = 0;
    if (cout == NULL) {
	warn ("No control connection for command");
	code = -1;
	return (0);
    }
    oldintr = signal(SIGINT, cmdabort);
    if(debug){
	printf("---> ");
	if (strncmp("PASS ", fmt, 5) == 0)
	    printf("PASS XXXX");
	else {
	    va_start(ap, fmt);
	    vfprintf(stdout, fmt, ap);
	    va_end(ap);
	}
    }
    va_start(ap, fmt);
    sec_vfprintf(cout, fmt, ap);
    va_end(ap);
    if(debug){
	printf("\n");
	fflush(stdout);
    }
    fprintf (cout, "\r\n");
    fflush (cout);
    cpend = 1;
    r = getreply (!strcmp (fmt, "QUIT"));
    if (abrtflag && oldintr != SIG_IGN)
	(*oldintr) (SIGINT);
    signal (SIGINT, oldintr);
    return (r);
}

char reply_string[BUFSIZ];	/* last line of previous reply */

int
getreply (int expecteof)
{
    char *p;
    char *lead_string;
    int c;
    struct sigaction sa, osa;
    char buf[8192];
    int reply_code;
    int long_warn = 0;

    sigemptyset (&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = cmdabort;
    sigaction (SIGINT, &sa, &osa);

    p = buf;

    reply_code = 0;
    while (1) {
	c = getc (cin);
	switch (c) {
	case EOF:
	    if (expecteof) {
		sigaction (SIGINT, &osa, NULL);
		code = 221;
		return 0;
	    }
	    lostpeer (0);
	    if (verbose) {
		printf ("421 Service not available, "
			"remote server has closed connection\n");
		fflush (stdout);
	    }
	    code = 421;
	    return (4);
	case IAC:
	    c = getc (cin);
	    if (c == WILL || c == WONT)
		fprintf (cout, "%c%c%c", IAC, DONT, getc (cin));
	    if (c == DO || c == DONT)
		fprintf (cout, "%c%c%c", IAC, WONT, getc (cin));
	    continue;
	case '\n':
	    *p++ = '\0';
	    if(isdigit((unsigned char)buf[0])){
		sscanf(buf, "%d", &code);
		if(code == 631){
		    code = 0;
		    sec_read_msg(buf, prot_safe);
		    sscanf(buf, "%d", &code);
		    lead_string = "S:";
		} else if(code == 632){
		    code = 0;
		    sec_read_msg(buf, prot_private);
		    sscanf(buf, "%d", &code);
		    lead_string = "P:";
		}else if(code == 633){
		    code = 0;
		    sec_read_msg(buf, prot_confidential);
		    sscanf(buf, "%d", &code);
		    lead_string = "C:";
		}else if(sec_complete)
		    lead_string = "!!";
		else
		    lead_string = "";
		if(code != 0 && reply_code == 0)
		    reply_code = code;
		if (verbose > 0 || (verbose > -1 && code > 499))
		    fprintf (stdout, "%s%s\n", lead_string, buf);
		if (code == reply_code && buf[3] == ' ') {
		    strlcpy (reply_string, buf, sizeof(reply_string));
		    if (code >= 200)
			cpend = 0;
		    sigaction (SIGINT, &osa, NULL);
		    if (code == 421)
			lostpeer (0);
#if 1
		    if (abrtflag &&
			osa.sa_handler != cmdabort &&
			osa.sa_handler != SIG_IGN)
			osa.sa_handler (SIGINT);
#endif
		    if (code == 227 || code == 229) {
			char *q;

			q = strchr (reply_string, '(');
			if (q) {
			    q++;
			    strlcpy(pasv, q, sizeof(pasv));
			    q = strrchr(pasv, ')');
			    if (q)
				*q = '\0';
			}
		    }
		    return code / 100;
		}
	    }else{
		if(verbose > 0 || (verbose > -1 && code > 499)){
		    if(sec_complete)
			fprintf(stdout, "!!");
		    fprintf(stdout, "%s\n", buf);
		}
	    }
	    p = buf;
	    long_warn = 0;
	    continue;
	default:
	    if(p < buf + sizeof(buf) - 1)
		*p++ = c;
	    else if(long_warn == 0) {
		fprintf(stderr, "WARNING: incredibly long line received\n");
		long_warn = 1;
	    }
	}
    }

}


#if 0
int
getreply (int expecteof)
{
    int c, n;
    int dig;
    int originalcode = 0, continuation = 0;
    sighand oldintr;
    int pflag = 0;
    char *cp, *pt = pasv;

    oldintr = signal (SIGINT, cmdabort);
    for (;;) {
	dig = n = code = 0;
	cp = reply_string;
	while ((c = getc (cin)) != '\n') {
	    if (c == IAC) {	/* handle telnet commands */
		switch (c = getc (cin)) {
		case WILL:
		case WONT:
		    c = getc (cin);
		    fprintf (cout, "%c%c%c", IAC, DONT, c);
		    fflush (cout);
		    break;
		case DO:
		case DONT:
		    c = getc (cin);
		    fprintf (cout, "%c%c%c", IAC, WONT, c);
		    fflush (cout);
		    break;
		default:
		    break;
		}
		continue;
	    }
	    dig++;
	    if (c == EOF) {
		if (expecteof) {
		    signal (SIGINT, oldintr);
		    code = 221;
		    return (0);
		}
		lostpeer (0);
		if (verbose) {
		    printf ("421 Service not available, remote server has closed connection\n");
		    fflush (stdout);
		}
		code = 421;
		return (4);
	    }
	    if (c != '\r' && (verbose > 0 ||
			      (verbose > -1 && n == '5' && dig > 4))) {
		if (proxflag &&
		    (dig == 1 || dig == 5 && verbose == 0))
		    printf ("%s:", hostname);
		putchar (c);
	    }
	    if (dig < 4 && isdigit (c))
		code = code * 10 + (c - '0');
	    if (!pflag && code == 227)
		pflag = 1;
	    if (dig > 4 && pflag == 1 && isdigit (c))
		pflag = 2;
	    if (pflag == 2) {
		if (c != '\r' && c != ')')
		    *pt++ = c;
		else {
		    *pt = '\0';
		    pflag = 3;
		}
	    }
	    if (dig == 4 && c == '-') {
		if (continuation)
		    code = 0;
		continuation++;
	    }
	    if (n == 0)
		n = c;
	    if (cp < &reply_string[sizeof (reply_string) - 1])
		*cp++ = c;
	}
	if (verbose > 0 || verbose > -1 && n == '5') {
	    putchar (c);
	    fflush (stdout);
	}
	if (continuation && code != originalcode) {
	    if (originalcode == 0)
		originalcode = code;
	    continue;
	}
	*cp = '\0';
	if(sec_complete){
	    if(code == 631)
		sec_read_msg(reply_string, prot_safe);
	    else if(code == 632)
		sec_read_msg(reply_string, prot_private);
	    else if(code == 633)
		sec_read_msg(reply_string, prot_confidential);
	    n = code / 100 + '0';
	}
	if (n != '1')
	    cpend = 0;
	signal (SIGINT, oldintr);
	if (code == 421 || originalcode == 421)
	    lostpeer (0);
	if (abrtflag && oldintr != cmdabort && oldintr != SIG_IGN)
	    (*oldintr) (SIGINT);
	return (n - '0');
    }
}

#endif

int
empty (fd_set * mask, int sec)
{
    struct timeval t;

    t.tv_sec = sec;
    t.tv_usec = 0;
    return (select (FD_SETSIZE, mask, NULL, NULL, &t));
}

jmp_buf sendabort;

static RETSIGTYPE
abortsend (int sig)
{

    mflag = 0;
    abrtflag = 0;
    printf ("\nsend aborted\nwaiting for remote to finish abort\n");
    fflush (stdout);
    longjmp (sendabort, 1);
}

#define HASHBYTES 1024

static int
copy_stream (FILE * from, FILE * to)
{
    static size_t bufsize;
    static char *buf;
    int n;
    int bytes = 0;
    int werr = 0;
    int hashbytes = HASHBYTES;
    struct stat st;

#if defined(HAVE_MMAP) && !defined(NO_MMAP)
    void *chunk;
    size_t off;

#define BLOCKSIZE (1024 * 1024 * 10)

#ifndef MAP_FAILED
#define MAP_FAILED (-1)
#endif

    if (fstat (fileno (from), &st) == 0 && S_ISREG (st.st_mode)) {
	/*
	 * mmap zero bytes has potential of loosing, don't do it.
	 */
	if (st.st_size == 0)
	    return 0;
	off = 0;
	while (off != st.st_size) {
	    size_t len;
	    ssize_t res;

	    len = st.st_size - off;
	    if (len > BLOCKSIZE)
		len = BLOCKSIZE;

	    chunk = mmap (0, len, PROT_READ, MAP_SHARED, fileno (from), off);
	    if (chunk == (void *) MAP_FAILED) {
		if (off == 0) /* try read if mmap doesn't work */
		    goto try_read;
		break;
	    }

	    res = sec_write (fileno (to), chunk, len);
	    if (msync (chunk, len, MS_ASYNC))
		warn ("msync");
	    if (munmap (chunk, len) < 0)
		warn ("munmap");
	    sec_fflush (to);
	    if (res != len)
		return off;
	    off += len;
	}
	return off;
    }
try_read:
#endif

    buf = alloc_buffer (buf, &bufsize,
			fstat (fileno (from), &st) >= 0 ? &st : NULL);
    if (buf == NULL)
	return -1;

    while ((n = read (fileno (from), buf, bufsize)) > 0) {
	werr = sec_write (fileno (to), buf, n);
	if (werr < 0)
	    break;
	bytes += werr;
	while (hash && bytes > hashbytes) {
	    putchar ('#');
	    hashbytes += HASHBYTES;
	}
    }
    sec_fflush (to);
    if (n < 0)
	warn ("local");

    if (werr < 0) {
	if (errno != EPIPE)
	    warn ("netout");
	bytes = -1;
    }
    return bytes;
}

void
sendrequest (char *cmd, char *local, char *remote, char *lmode, int printnames)
{
    struct stat st;
    struct timeval start, stop;
    int c, d;
    FILE *fin, *dout = 0;
    int (*closefunc) (FILE *);
    RETSIGTYPE (*oldintr)(int), (*oldintp)(int);
    long bytes = 0, hashbytes = HASHBYTES;
    char *rmode = "w";

    if (verbose && printnames) {
	if (strcmp (local, "-") != 0)
	    printf ("local: %s ", local);
	if (remote)
	    printf ("remote: %s\n", remote);
    }
    if (proxy) {
	proxtrans (cmd, local, remote);
	return;
    }
    if (curtype != type)
	changetype (type, 0);
    closefunc = NULL;
    oldintr = NULL;
    oldintp = NULL;

    if (setjmp (sendabort)) {
	while (cpend) {
	    getreply (0);
	}
	if (data >= 0) {
	    close (data);
	    data = -1;
	}
	if (oldintr)
	    signal (SIGINT, oldintr);
	if (oldintp)
	    signal (SIGPIPE, oldintp);
	code = -1;
	return;
    }
    oldintr = signal (SIGINT, abortsend);
    if (strcmp (local, "-") == 0)
	fin = stdin;
    else if (*local == '|') {
	oldintp = signal (SIGPIPE, SIG_IGN);
	fin = popen (local + 1, lmode);
	if (fin == NULL) {
	    warn ("%s", local + 1);
	    signal (SIGINT, oldintr);
	    signal (SIGPIPE, oldintp);
	    code = -1;
	    return;
	}
	closefunc = pclose;
    } else {
	fin = fopen (local, lmode);
	if (fin == NULL) {
	    warn ("local: %s", local);
	    signal (SIGINT, oldintr);
	    code = -1;
	    return;
	}
	closefunc = fclose;
	if (fstat (fileno (fin), &st) < 0 || !S_ISREG(st.st_mode)) {
	    fprintf (stdout, "%s: not a plain file.\n", local);
	    signal (SIGINT, oldintr);
	    fclose (fin);
	    code = -1;
	    return;
	}
    }
    if (initconn ()) {
	signal (SIGINT, oldintr);
	if (oldintp)
	    signal (SIGPIPE, oldintp);
	code = -1;
	if (closefunc != NULL)
	    (*closefunc) (fin);
	return;
    }
    if (setjmp (sendabort))
	goto abort;

    if (restart_point &&
	(strcmp (cmd, "STOR") == 0 || strcmp (cmd, "APPE") == 0)) {
	int rc;

	switch (curtype) {
	case TYPE_A:
	    rc = fseek (fin, (long) restart_point, SEEK_SET);
	    break;
	case TYPE_I:
	case TYPE_L:
	    rc = lseek (fileno (fin), restart_point, SEEK_SET);
	    break;
	default:
	    abort();
	}
	if (rc < 0) {
	    warn ("local: %s", local);
	    restart_point = 0;
	    if (closefunc != NULL)
		(*closefunc) (fin);
	    return;
	}
	if (command ("REST %ld", (long) restart_point)
	    != CONTINUE) {
	    restart_point = 0;
	    if (closefunc != NULL)
		(*closefunc) (fin);
	    return;
	}
	restart_point = 0;
	rmode = "r+w";
    }
    if (remote) {
	if (command ("%s %s", cmd, remote) != PRELIM) {
	    signal (SIGINT, oldintr);
	    if (oldintp)
		signal (SIGPIPE, oldintp);
	    if (closefunc != NULL)
		(*closefunc) (fin);
	    return;
	}
    } else if (command ("%s", cmd) != PRELIM) {
	    signal(SIGINT, oldintr);
	    if (oldintp)
		signal(SIGPIPE, oldintp);
	    if (closefunc != NULL)
		(*closefunc)(fin);
	    return;
	}
    dout = dataconn(rmode);
    if (dout == NULL)
	goto abort;
    set_buffer_size (fileno (dout), 0);
    gettimeofday (&start, (struct timezone *) 0);
    oldintp = signal (SIGPIPE, SIG_IGN);
    switch (curtype) {

    case TYPE_I:
    case TYPE_L:
	errno = d = c = 0;
	bytes = copy_stream (fin, dout);
	break;

    case TYPE_A:
	while ((c = getc (fin)) != EOF) {
	    if (c == '\n') {
		while (hash && (bytes >= hashbytes)) {
		    putchar ('#');
		    fflush (stdout);
		    hashbytes += HASHBYTES;
		}
		if (ferror (dout))
		    break;
		sec_putc ('\r', dout);
		bytes++;
	    }
	    sec_putc (c, dout);
	    bytes++;
	}
	sec_fflush (dout);
	if (hash) {
	    if (bytes < hashbytes)
		putchar ('#');
	    putchar ('\n');
	    fflush (stdout);
	}
	if (ferror (fin))
	    warn ("local: %s", local);
	if (ferror (dout)) {
	    if (errno != EPIPE)
		warn ("netout");
	    bytes = -1;
	}
	break;
    }
    if (closefunc != NULL)
	(*closefunc) (fin);
    fclose (dout);
    gettimeofday (&stop, (struct timezone *) 0);
    getreply (0);
    signal (SIGINT, oldintr);
    if (oldintp)
	signal (SIGPIPE, oldintp);
    if (bytes > 0)
	ptransfer ("sent", bytes, &start, &stop);
    return;
abort:
    signal (SIGINT, oldintr);
    if (oldintp)
	signal (SIGPIPE, oldintp);
    if (!cpend) {
	code = -1;
	return;
    }
    if (data >= 0) {
	close (data);
	data = -1;
    }
    if (dout)
	fclose (dout);
    getreply (0);
    code = -1;
    if (closefunc != NULL && fin != NULL)
	(*closefunc) (fin);
    gettimeofday (&stop, (struct timezone *) 0);
    if (bytes > 0)
	ptransfer ("sent", bytes, &start, &stop);
}

jmp_buf recvabort;

void
abortrecv (int sig)
{

    mflag = 0;
    abrtflag = 0;
    printf ("\nreceive aborted\nwaiting for remote to finish abort\n");
    fflush (stdout);
    longjmp (recvabort, 1);
}

void
recvrequest (char *cmd, char *local, char *remote,
	     char *lmode, int printnames, int local_given)
{
    FILE *fout = NULL, *din = NULL;
    int (*closefunc) (FILE *);
    sighand oldintr, oldintp;
    int c, d, is_retr, tcrflag, bare_lfs = 0;
    static size_t bufsize;
    static char *buf;
    long bytes = 0, hashbytes = HASHBYTES;
    struct timeval start, stop;
    struct stat st;

    is_retr = strcmp (cmd, "RETR") == 0;
    if (is_retr && verbose && printnames) {
	if (strcmp (local, "-") != 0)
	    printf ("local: %s ", local);
	if (remote)
	    printf ("remote: %s\n", remote);
    }
    if (proxy && is_retr) {
	proxtrans (cmd, local, remote);
	return;
    }
    closefunc = NULL;
    oldintr = NULL;
    oldintp = NULL;
    tcrflag = !crflag && is_retr;
    if (setjmp (recvabort)) {
	while (cpend) {
	    getreply (0);
	}
	if (data >= 0) {
	    close (data);
	    data = -1;
	}
	if (oldintr)
	    signal (SIGINT, oldintr);
	code = -1;
	return;
    }
    oldintr = signal (SIGINT, abortrecv);
    if (!local_given || (strcmp(local, "-") && *local != '|')) {
	if (access (local, 2) < 0) {
	    char *dir = strrchr (local, '/');

	    if (errno != ENOENT && errno != EACCES) {
		warn ("local: %s", local);
		signal (SIGINT, oldintr);
		code = -1;
		return;
	    }
	    if (dir != NULL)
		*dir = 0;
	    d = access (dir ? local : ".", 2);
	    if (dir != NULL)
		*dir = '/';
	    if (d < 0) {
		warn ("local: %s", local);
		signal (SIGINT, oldintr);
		code = -1;
		return;
	    }
	    if (!runique && errno == EACCES &&
		chmod (local, 0600) < 0) {
		warn ("local: %s", local);
		signal (SIGINT, oldintr);
		signal (SIGINT, oldintr);
		code = -1;
		return;
	    }
	    if (runique && errno == EACCES &&
		(local = gunique (local)) == NULL) {
		signal (SIGINT, oldintr);
		code = -1;
		return;
	    }
	} else if (runique && (local = gunique (local)) == NULL) {
	    signal(SIGINT, oldintr);
	    code = -1;
	    return;
	}
    }
    if (!is_retr) {
	if (curtype != TYPE_A)
	    changetype (TYPE_A, 0);
    } else if (curtype != type)
	changetype (type, 0);
    if (initconn ()) {
	signal (SIGINT, oldintr);
	code = -1;
	return;
    }
    if (setjmp (recvabort))
	goto abort;
    if (is_retr && restart_point &&
	command ("REST %ld", (long) restart_point) != CONTINUE)
	return;
    if (remote) {
	if (command ("%s %s", cmd, remote) != PRELIM) {
	    signal (SIGINT, oldintr);
	    return;
	}
    } else {
	if (command ("%s", cmd) != PRELIM) {
	    signal (SIGINT, oldintr);
	    return;
	}
    }
    din = dataconn ("r");
    if (din == NULL)
	goto abort;
    set_buffer_size (fileno (din), 1);
    if (local_given && strcmp (local, "-") == 0)
	fout = stdout;
    else if (local_given && *local == '|') {
	oldintp = signal (SIGPIPE, SIG_IGN);
	fout = popen (local + 1, "w");
	if (fout == NULL) {
	    warn ("%s", local + 1);
	    goto abort;
	}
	closefunc = pclose;
    } else {
	fout = fopen (local, lmode);
	if (fout == NULL) {
	    warn ("local: %s", local);
	    goto abort;
	}
	closefunc = fclose;
    }
    buf = alloc_buffer (buf, &bufsize,
			fstat (fileno (fout), &st) >= 0 ? &st : NULL);
    if (buf == NULL)
	goto abort;

    gettimeofday (&start, (struct timezone *) 0);
    switch (curtype) {

    case TYPE_I:
    case TYPE_L:
	if (restart_point &&
	    lseek (fileno (fout), restart_point, SEEK_SET) < 0) {
	    warn ("local: %s", local);
	    if (closefunc != NULL)
		(*closefunc) (fout);
	    return;
	}
	errno = d = 0;
	while ((c = sec_read (fileno (din), buf, bufsize)) > 0) {
	    if ((d = write (fileno (fout), buf, c)) != c)
		break;
	    bytes += c;
	    if (hash) {
		while (bytes >= hashbytes) {
		    putchar ('#');
		    hashbytes += HASHBYTES;
		}
		fflush (stdout);
	    }
	}
	if (hash && bytes > 0) {
	    if (bytes < HASHBYTES)
		putchar ('#');
	    putchar ('\n');
	    fflush (stdout);
	}
	if (c < 0) {
	    if (errno != EPIPE)
		warn ("netin");
	    bytes = -1;
	}
	if (d < c) {
	    if (d < 0)
		warn ("local: %s", local);
	    else
		warnx ("%s: short write", local);
	}
	break;

    case TYPE_A:
	if (restart_point) {
	    int i, n, ch;

	    if (fseek (fout, 0L, SEEK_SET) < 0)
		goto done;
	    n = restart_point;
	    for (i = 0; i++ < n;) {
		if ((ch = sec_getc (fout)) == EOF)
		    goto done;
		if (ch == '\n')
		    i++;
	    }
	    if (fseek (fout, 0L, SEEK_CUR) < 0) {
	done:
		warn ("local: %s", local);
		if (closefunc != NULL)
		    (*closefunc) (fout);
		return;
	    }
	}
	while ((c = sec_getc(din)) != EOF) {
	    if (c == '\n')
		bare_lfs++;
	    while (c == '\r') {
		while (hash && (bytes >= hashbytes)) {
		    putchar ('#');
		    fflush (stdout);
		    hashbytes += HASHBYTES;
		}
		bytes++;
		if ((c = sec_getc (din)) != '\n' || tcrflag) {
		    if (ferror (fout))
			goto break2;
		    putc ('\r', fout);
		    if (c == '\0') {
			bytes++;
			goto contin2;
		    }
		    if (c == EOF)
			goto contin2;
		}
	    }
	    putc (c, fout);
	    bytes++;
    contin2:;
	}
break2:
	if (bare_lfs) {
	    printf ("WARNING! %d bare linefeeds received in ASCII mode\n",
		    bare_lfs);
	    printf ("File may not have transferred correctly.\n");
	}
	if (hash) {
	    if (bytes < hashbytes)
		putchar ('#');
	    putchar ('\n');
	    fflush (stdout);
	}
	if (ferror (din)) {
	    if (errno != EPIPE)
		warn ("netin");
	    bytes = -1;
	}
	if (ferror (fout))
	    warn ("local: %s", local);
	break;
    }
    if (closefunc != NULL)
	(*closefunc) (fout);
    signal (SIGINT, oldintr);
    if (oldintp)
	signal (SIGPIPE, oldintp);
    fclose (din);
    gettimeofday (&stop, (struct timezone *) 0);
    getreply (0);
    if (bytes > 0 && is_retr)
	ptransfer ("received", bytes, &start, &stop);
    return;
abort:

    /* abort using RFC959 recommended IP,SYNC sequence  */

    if (oldintp)
	signal (SIGPIPE, oldintr);
    signal (SIGINT, SIG_IGN);
    if (!cpend) {
	code = -1;
	signal (SIGINT, oldintr);
	return;
    }
    abort_remote(din);
    code = -1;
    if (data >= 0) {
	close (data);
	data = -1;
    }
    if (closefunc != NULL && fout != NULL)
	(*closefunc) (fout);
    if (din)
	fclose (din);
    gettimeofday (&stop, (struct timezone *) 0);
    if (bytes > 0)
	ptransfer ("received", bytes, &start, &stop);
    signal (SIGINT, oldintr);
}

static int
parse_epsv (const char *str)
{
    char sep;
    char *end;
    int port;

    if (*str == '\0')
	return -1;
    sep = *str++;
    if (sep != *str++)
	return -1;
    if (sep != *str++)
	return -1;
    port = strtol (str, &end, 0);
    if (str == end)
	return -1;
    if (end[0] != sep || end[1] != '\0')
	return -1;
    return htons(port);
}

static int
parse_pasv (struct sockaddr_in *sin4, const char *str)
{
    int a0, a1, a2, a3, p0, p1;

    /*
     * What we've got at this point is a string of comma separated
     * one-byte unsigned integer values. The first four are the an IP
     * address. The fifth is the MSB of the port number, the sixth is the
     * LSB. From that we'll prepare a sockaddr_in.
     */

    if (sscanf (str, "%d,%d,%d,%d,%d,%d",
		&a0, &a1, &a2, &a3, &p0, &p1) != 6) {
	printf ("Passive mode address scan failure. "
		"Shouldn't happen!\n");
	return -1;
    }
    if (a0 < 0 || a0 > 255 ||
	a1 < 0 || a1 > 255 ||
	a2 < 0 || a2 > 255 ||
	a3 < 0 || a3 > 255 ||
	p0 < 0 || p0 > 255 ||
	p1 < 0 || p1 > 255) {
	printf ("Can't parse passive mode string.\n");
	return -1;
    }
    memset (sin4, 0, sizeof(*sin4));
    sin4->sin_family      = AF_INET;
    sin4->sin_addr.s_addr = htonl ((a0 << 24) | (a1 << 16) |
				  (a2 << 8) | a3);
    sin4->sin_port = htons ((p0 << 8) | p1);
    return 0;
}

static int
passive_mode (void)
{
    int port;

    data = socket (myctladdr->sa_family, SOCK_STREAM, 0);
    if (data < 0) {
	warn ("socket");
	return (1);
    }
    if (options & SO_DEBUG)
	socket_set_debug (data);
    if (command ("EPSV") != COMPLETE) {
	if (command ("PASV") != COMPLETE) {
	    printf ("Passive mode refused.\n");
	    goto bad;
	}
    }

    /*
     * Parse the reply to EPSV or PASV
     */

    port = parse_epsv (pasv);
    if (port > 0) {
	data_addr->sa_family = myctladdr->sa_family;
	socket_set_address_and_port (data_addr,
				     socket_get_address (hisctladdr),
				     port);
    } else {
	if (parse_pasv ((struct sockaddr_in *)data_addr, pasv) < 0)
	    goto bad;
    }

    if (connect (data, data_addr, socket_sockaddr_size (data_addr)) < 0) {
	warn ("connect");
	goto bad;
    }
#ifdef IPTOS_THROUGHPUT
    socket_set_tos (data, IPTOS_THROUGHPUT);
#endif
    return (0);
bad:
    close (data);
    data = -1;
    sendport = 1;
    return (1);
}


static int
active_mode (void)
{
    int tmpno = 0;
    socklen_t len;
    int result;

noport:
    data_addr->sa_family = myctladdr->sa_family;
    socket_set_address_and_port (data_addr, socket_get_address (myctladdr),
				 sendport ? 0 : socket_get_port (myctladdr));

    if (data != -1)
	close (data);
    data = socket (data_addr->sa_family, SOCK_STREAM, 0);
    if (data < 0) {
	warn ("socket");
	if (tmpno)
	    sendport = 1;
	return (1);
    }
    if (!sendport)
	socket_set_reuseaddr (data, 1);
    if (bind (data, data_addr, socket_sockaddr_size (data_addr)) < 0) {
	warn ("bind");
	goto bad;
    }
    if (options & SO_DEBUG)
	socket_set_debug (data);
    len = sizeof (data_addr_ss);
    if (getsockname (data, data_addr, &len) < 0) {
	warn ("getsockname");
	goto bad;
    }
    if (listen (data, 1) < 0)
	warn ("listen");
    if (sendport) {
	char addr_str[256];
	int inet_af;
	int overbose;

	if (inet_ntop (data_addr->sa_family, socket_get_address (data_addr),
		       addr_str, sizeof(addr_str)) == NULL)
	    errx (1, "inet_ntop failed");
	switch (data_addr->sa_family) {
	case AF_INET :
	    inet_af = 1;
	    break;
#ifdef HAVE_IPV6
	case AF_INET6 :
	    inet_af = 2;
	    break;
#endif
	default :
	    errx (1, "bad address family %d", data_addr->sa_family);
	}


	overbose = verbose;
	if (debug == 0)
	    verbose  = -1;

	result = command ("EPRT |%d|%s|%d|",
			  inet_af, addr_str,
			  ntohs(socket_get_port (data_addr)));
	verbose = overbose;

	if (result == ERROR) {
	    struct sockaddr_in *sin4 = (struct sockaddr_in *)data_addr;

	    unsigned int a = ntohl(sin4->sin_addr.s_addr);
	    unsigned int p = ntohs(sin4->sin_port);

	    if (data_addr->sa_family != AF_INET) {
		warnx ("remote server doesn't support EPRT");
		goto bad;
	    }

	    result = command("PORT %d,%d,%d,%d,%d,%d",
			     (a >> 24) & 0xff,
			     (a >> 16) & 0xff,
			     (a >> 8) & 0xff,
			     a & 0xff,
			     (p >> 8) & 0xff,
			     p & 0xff);
	    if (result == ERROR && sendport == -1) {
		sendport = 0;
		tmpno = 1;
		goto noport;
	    }
	    return (result != COMPLETE);
	}
	return result != COMPLETE;
    }
    if (tmpno)
	sendport = 1;


#ifdef IPTOS_THROUGHPUT
    socket_set_tos (data, IPTOS_THROUGHPUT);
#endif
    return (0);
bad:
    close (data);
    data = -1;
    if (tmpno)
	sendport = 1;
    return (1);
}

/*
 * Need to start a listen on the data channel before we send the command,
 * otherwise the server's connect may fail.
 */
int
initconn (void)
{
    if (passivemode)
	return passive_mode ();
    else
	return active_mode ();
}

FILE *
dataconn (const char *lmode)
{
    struct sockaddr_storage from_ss;
    struct sockaddr *from = (struct sockaddr *)&from_ss;
    socklen_t fromlen = sizeof(from_ss);
    int s;

    if (passivemode)
	return (fdopen (data, lmode));

    s = accept (data, from, &fromlen);
    if (s < 0) {
	warn ("accept");
	close (data), data = -1;
	return (NULL);
    }
    close (data);
    data = s;
#ifdef IPTOS_THROUGHPUT
    socket_set_tos (s, IPTOS_THROUGHPUT);
#endif
    return (fdopen (data, lmode));
}

void
ptransfer (char *direction, long int bytes,
	   struct timeval * t0, struct timeval * t1)
{
    struct timeval td;
    float s;
    float bs;
    int prec;
    char *unit;

    if (verbose) {
	td.tv_sec = t1->tv_sec - t0->tv_sec;
	td.tv_usec = t1->tv_usec - t0->tv_usec;
	if (td.tv_usec < 0) {
	    td.tv_sec--;
	    td.tv_usec += 1000000;
	}
	s = td.tv_sec + (td.tv_usec / 1000000.);
	bs = bytes / (s ? s : 1);
	if (bs >= 1048576) {
	    bs /= 1048576;
	    unit = "M";
	    prec = 2;
	} else if (bs >= 1024) {
	    bs /= 1024;
	    unit = "k";
	    prec = 1;
	} else {
	    unit = "";
	    prec = 0;
	}

	printf ("%ld bytes %s in %.3g seconds (%.*f %sbyte/s)\n",
		bytes, direction, s, prec, bs, unit);
    }
}

void
psabort (int sig)
{

    abrtflag++;
}

void
pswitch (int flag)
{
    sighand oldintr;
    static struct comvars {
	int connect;
	char name[MaxHostNameLen];
	struct sockaddr_storage mctl;
	struct sockaddr_storage hctl;
	FILE *in;
	FILE *out;
	int tpe;
	int curtpe;
	int cpnd;
	int sunqe;
	int runqe;
	int mcse;
	int ntflg;
	char nti[17];
	char nto[17];
	int mapflg;
	char mi[MaxPathLen];
	char mo[MaxPathLen];
    } proxstruct, tmpstruct;
    struct comvars *ip, *op;

    abrtflag = 0;
    oldintr = signal (SIGINT, psabort);
    if (flag) {
	if (proxy)
	    return;
	ip = &tmpstruct;
	op = &proxstruct;
	proxy++;
    } else {
	if (!proxy)
	    return;
	ip = &proxstruct;
	op = &tmpstruct;
	proxy = 0;
    }
    ip->connect = connected;
    connected = op->connect;
    if (hostname) {
	strlcpy (ip->name, hostname, sizeof (ip->name));
    } else
	ip->name[0] = 0;
    hostname = op->name;
    ip->hctl = hisctladdr_ss;
    hisctladdr_ss = op->hctl;
    ip->mctl = myctladdr_ss;
    myctladdr_ss = op->mctl;
    ip->in = cin;
    cin = op->in;
    ip->out = cout;
    cout = op->out;
    ip->tpe = type;
    type = op->tpe;
    ip->curtpe = curtype;
    curtype = op->curtpe;
    ip->cpnd = cpend;
    cpend = op->cpnd;
    ip->sunqe = sunique;
    sunique = op->sunqe;
    ip->runqe = runique;
    runique = op->runqe;
    ip->mcse = mcase;
    mcase = op->mcse;
    ip->ntflg = ntflag;
    ntflag = op->ntflg;
    strlcpy (ip->nti, ntin, sizeof (ip->nti));
    strlcpy (ntin, op->nti, 17);
    strlcpy (ip->nto, ntout, sizeof (ip->nto));
    strlcpy (ntout, op->nto, 17);
    ip->mapflg = mapflag;
    mapflag = op->mapflg;
    strlcpy (ip->mi, mapin, MaxPathLen);
    strlcpy (mapin, op->mi, MaxPathLen);
    strlcpy (ip->mo, mapout, MaxPathLen);
    strlcpy (mapout, op->mo, MaxPathLen);
    signal(SIGINT, oldintr);
    if (abrtflag) {
	abrtflag = 0;
	(*oldintr) (SIGINT);
    }
}

void
abortpt (int sig)
{

    printf ("\n");
    fflush (stdout);
    ptabflg++;
    mflag = 0;
    abrtflag = 0;
    longjmp (ptabort, 1);
}

void
proxtrans (char *cmd, char *local, char *remote)
{
    sighand oldintr = NULL;
    int secndflag = 0, prox_type, nfnd;
    char *cmd2;
    fd_set mask;

    if (strcmp (cmd, "RETR"))
	cmd2 = "RETR";
    else
	cmd2 = runique ? "STOU" : "STOR";
    if ((prox_type = type) == 0) {
	if (unix_server && unix_proxy)
	    prox_type = TYPE_I;
	else
	    prox_type = TYPE_A;
    }
    if (curtype != prox_type)
	changetype (prox_type, 1);
    if (command ("PASV") != COMPLETE) {
	printf ("proxy server does not support third party transfers.\n");
	return;
    }
    pswitch (0);
    if (!connected) {
	printf ("No primary connection\n");
	pswitch (1);
	code = -1;
	return;
    }
    if (curtype != prox_type)
	changetype (prox_type, 1);
    if (command ("PORT %s", pasv) != COMPLETE) {
	pswitch (1);
	return;
    }
    if (setjmp (ptabort))
	goto abort;
    oldintr = signal (SIGINT, abortpt);
    if (command ("%s %s", cmd, remote) != PRELIM) {
	signal (SIGINT, oldintr);
	pswitch (1);
	return;
    }
    sleep (2);
    pswitch (1);
    secndflag++;
    if (command ("%s %s", cmd2, local) != PRELIM)
	goto abort;
    ptflag++;
    getreply (0);
    pswitch (0);
    getreply (0);
    signal (SIGINT, oldintr);
    pswitch (1);
    ptflag = 0;
    printf ("local: %s remote: %s\n", local, remote);
    return;
abort:
    signal (SIGINT, SIG_IGN);
    ptflag = 0;
    if (strcmp (cmd, "RETR") && !proxy)
	pswitch (1);
    else if (!strcmp (cmd, "RETR") && proxy)
	pswitch (0);
    if (!cpend && !secndflag) {	/* only here if cmd = "STOR" (proxy=1) */
	if (command ("%s %s", cmd2, local) != PRELIM) {
	    pswitch (0);
	    if (cpend)
		abort_remote ((FILE *) NULL);
	}
	pswitch (1);
	if (ptabflg)
	    code = -1;
	if (oldintr)
	    signal (SIGINT, oldintr);
	return;
    }
    if (cpend)
	abort_remote ((FILE *) NULL);
    pswitch (!proxy);
    if (!cpend && !secndflag) {	/* only if cmd = "RETR" (proxy=1) */
	if (command ("%s %s", cmd2, local) != PRELIM) {
	    pswitch (0);
	    if (cpend)
		abort_remote ((FILE *) NULL);
	    pswitch (1);
	    if (ptabflg)
		code = -1;
	    signal (SIGINT, oldintr);
	    return;
	}
    }
    if (cpend)
	abort_remote ((FILE *) NULL);
    pswitch (!proxy);
    if (cpend) {
	FD_ZERO (&mask);
	if (fileno(cin) >= FD_SETSIZE)
	    errx (1, "fd too large");
	FD_SET (fileno (cin), &mask);
	if ((nfnd = empty (&mask, 10)) <= 0) {
	    if (nfnd < 0) {
		warn ("abort");
	    }
	    if (ptabflg)
		code = -1;
	    lostpeer (0);
	}
	getreply (0);
	getreply (0);
    }
    if (proxy)
	pswitch (0);
    pswitch (1);
    if (ptabflg)
	code = -1;
    signal (SIGINT, oldintr);
}

void
reset (int argc, char **argv)
{
    fd_set mask;
    int nfnd = 1;

    FD_ZERO (&mask);
    while (nfnd > 0) {
	if (fileno (cin) >= FD_SETSIZE)
	    errx (1, "fd too large");
	FD_SET (fileno (cin), &mask);
	if ((nfnd = empty (&mask, 0)) < 0) {
	    warn ("reset");
	    code = -1;
	    lostpeer(0);
	} else if (nfnd) {
	    getreply(0);
	}
    }
}

char *
gunique (char *local)
{
    static char new[MaxPathLen];
    char *cp = strrchr (local, '/');
    int d, count = 0;
    char ext = '1';

    if (cp)
	*cp = '\0';
    d = access (cp ? local : ".", 2);
    if (cp)
	*cp = '/';
    if (d < 0) {
	warn ("local: %s", local);
	return NULL;
    }
    strlcpy (new, local, sizeof(new));
    cp = new + strlen(new);
    *cp++ = '.';
    while (!d) {
	if (++count == 100) {
	    printf ("runique: can't find unique file name.\n");
	    return NULL;
	}
	*cp++ = ext;
	*cp = '\0';
	if (ext == '9')
	    ext = '0';
	else
	    ext++;
	if ((d = access (new, 0)) < 0)
	    break;
	if (ext != '0')
	    cp--;
	else if (*(cp - 2) == '.')
	    *(cp - 1) = '1';
	else {
	    *(cp - 2) = *(cp - 2) + 1;
	    cp--;
	}
    }
    return (new);
}

void
abort_remote (FILE * din)
{
    char buf[BUFSIZ];
    int nfnd;
    fd_set mask;

    /*
     * send IAC in urgent mode instead of DM because 4.3BSD places oob mark
     * after urgent byte rather than before as is protocol now
     */
    snprintf (buf, sizeof (buf), "%c%c%c", IAC, IP, IAC);
    if (send (fileno (cout), buf, 3, MSG_OOB) != 3)
	warn ("abort");
    fprintf (cout, "%c", DM);
    sec_fprintf(cout, "ABOR");
    sec_fflush (cout);
    fprintf (cout, "\r\n");
    fflush(cout);
    FD_ZERO (&mask);
    if (fileno (cin) >= FD_SETSIZE)
	errx (1, "fd too large");
    FD_SET (fileno (cin), &mask);
    if (din) {
	if (fileno (din) >= FD_SETSIZE)
	    errx (1, "fd too large");
	FD_SET (fileno (din), &mask);
    }
    if ((nfnd = empty (&mask, 10)) <= 0) {
	if (nfnd < 0) {
	    warn ("abort");
	}
	if (ptabflg)
	    code = -1;
	lostpeer (0);
    }
    if (din && FD_ISSET (fileno (din), &mask)) {
	while (read (fileno (din), buf, BUFSIZ) > 0)
	     /* LOOP */ ;
    }
    if (getreply (0) == ERROR && code == 552) {
	/* 552 needed for nic style abort */
	getreply (0);
    }
    getreply (0);
}
