/*
 * Copyright (c) 2000 Andre Lucas.  All rights reserved.
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

/**
 ** logintest.c:  simple test driver for platform-independent login recording
 **               and lastlog retrieval
 **/

#include "includes.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>

#include <netinet/in.h>

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pwd.h>
#include <netdb.h>
#ifdef HAVE_TIME_H
#include <time.h>
#endif

#include "loginrec.h"

extern char *__progname;

#define PAUSE_BEFORE_LOGOUT 3

int nologtest = 0;
int compile_opts_only = 0;
int be_verbose = 0;


/* Dump a logininfo to stdout. Assumes a tab size of 8 chars. */
void
dump_logininfo(struct logininfo *li, char *descname)
{
	/* yes I know how nasty this is */
	printf("struct logininfo %s = {\n\t"
	       "progname\t'%s'\n\ttype\t\t%d\n\t"
	       "pid\t\t%d\n\tuid\t\t%d\n\t"
	       "line\t\t'%s'\n\tusername\t'%s'\n\t"
	       "hostname\t'%s'\n\texit\t\t%d\n\ttermination\t%d\n\t"
	       "tv_sec\t%d\n\ttv_usec\t%d\n\t"
	       "struct login_netinfo hostaddr {\n\t\t"
	       "struct sockaddr sa {\n"
	       "\t\t\tfamily\t%d\n\t\t}\n"
	       "\t}\n"
	       "}\n",
	       descname, li->progname, li->type,
	       li->pid, li->uid, li->line,
	       li->username, li->hostname, li->exit,
	       li->termination, li->tv_sec, li->tv_usec,
	       li->hostaddr.sa.sa_family);
}


int
testAPI()
{
	struct logininfo *li1;
	struct passwd *pw;
	struct hostent *he;
	struct sockaddr_in sa_in4;
	char cmdstring[256], stripline[8];
	char username[32];
#ifdef HAVE_TIME_H
	time_t t0, t1, t2, logintime, logouttime;
	char s_t0[64],s_t1[64],s_t2[64];
	char s_logintime[64], s_logouttime[64]; /* ctime() strings */
#endif

	printf("**\n** Testing the API...\n**\n");

	pw = getpwuid(getuid());
	strlcpy(username, pw->pw_name, sizeof(username));

	/* gethostname(hostname, sizeof(hostname)); */

	printf("login_alloc_entry test (no host info):\n");

	/* FIXME fake tty more effectively - this could upset some platforms */
	li1 = login_alloc_entry((int)getpid(), username, NULL, ttyname(0));
	strlcpy(li1->progname, "OpenSSH-logintest", sizeof(li1->progname));

	if (be_verbose)
		dump_logininfo(li1, "li1");

	printf("Setting host address info for 'localhost' (may call out):\n");
	if (! (he = gethostbyname("localhost"))) {
		printf("Couldn't set hostname(lookup failed)\n");
	} else {
		/* NOTE: this is messy, but typically a program wouldn't have to set
		 *  any of this, a sockaddr_in* would be already prepared */
		memcpy((void *)&(sa_in4.sin_addr), (void *)&(he->h_addr_list[0][0]),
		       sizeof(struct in_addr));
		login_set_addr(li1, (struct sockaddr *) &sa_in4, sizeof(sa_in4));
		strlcpy(li1->hostname, "localhost", sizeof(li1->hostname));
	}
	if (be_verbose)
		dump_logininfo(li1, "li1");

	if ((int)geteuid() != 0) {
		printf("NOT RUNNING LOGIN TESTS - you are not root!\n");
		return 1;
	}

	if (nologtest)
		return 1;

	line_stripname(stripline, li1->line, sizeof(stripline));

	printf("Performing an invalid login attempt (no type field)\n--\n");
	login_write(li1);
	printf("--\n(Should have written errors to stderr)\n");

#ifdef HAVE_TIME_H
	(void)time(&t0);
	strlcpy(s_t0, ctime(&t0), sizeof(s_t0));
	t1 = login_get_lastlog_time(getuid());
	strlcpy(s_t1, ctime(&t1), sizeof(s_t1));
	printf("Before logging in:\n\tcurrent time is %d - %s\t"
	       "lastlog time is %d - %s\n",
	       (int)t0, s_t0, (int)t1, s_t1);
#endif

	printf("Performing a login on line %s ", stripline);
#ifdef HAVE_TIME_H
	(void)time(&logintime);
	strlcpy(s_logintime, ctime(&logintime), sizeof(s_logintime));
	printf("at %d - %s", (int)logintime, s_logintime);
#endif
	printf("--\n");
	login_login(li1);

	snprintf(cmdstring, sizeof(cmdstring), "who | grep '%s '",
		 stripline);
	system(cmdstring);

	printf("--\nPausing for %d second(s)...\n", PAUSE_BEFORE_LOGOUT);
	sleep(PAUSE_BEFORE_LOGOUT);

	printf("Performing a logout ");
#ifdef HAVE_TIME_H
	(void)time(&logouttime);
	strlcpy(s_logouttime, ctime(&logouttime), sizeof(s_logouttime));
	printf("at %d - %s", (int)logouttime, s_logouttime);
#endif
	printf("\nThe root login shown above should be gone.\n"
	       "If the root login hasn't gone, but another user on the same\n"
	       "pty has, this is OK - we're hacking it here, and there\n"
	       "shouldn't be two users on one pty in reality...\n"
	       "-- ('who' output follows)\n");
	login_logout(li1);

	system(cmdstring);
	printf("-- ('who' output ends)\n");

#ifdef HAVE_TIME_H
	t2 = login_get_lastlog_time(getuid());
	strlcpy(s_t2, ctime(&t2), sizeof(s_t2));
	printf("After logging in, lastlog time is %d - %s\n", (int)t2, s_t2);
	if (t1 == t2)
		printf("The lastlog times before and after logging in are the "
		       "same.\nThis indicates that lastlog is ** NOT WORKING "
		       "CORRECTLY **\n");
	else if (t0 != t2)
		/* We can be off by a second or so, even when recording works fine.
		 * I'm not 100% sure why, but it's true. */
		printf("** The login time and the lastlog time differ.\n"
		       "** This indicates that lastlog is either recording the "
		       "wrong time,\n** or retrieving the wrong entry.\n"
		       "If it's off by less than %d second(s) "
		       "run the test again.\n", PAUSE_BEFORE_LOGOUT);
	else
		printf("lastlog agrees with the login time. This is a good thing.\n");

#endif

	printf("--\nThe output of 'last' shown next should have "
	       "an entry for root \n  on %s for the time shown above:\n--\n",
	       stripline);
	snprintf(cmdstring, sizeof(cmdstring), "last | grep '%s ' | head -3",
		 stripline);
	system(cmdstring);

	printf("--\nEnd of login test.\n");

	login_free_entry(li1);

	return 1;
} /* testAPI() */


void
testLineName(char *line)
{
	/* have to null-terminate - these functions are designed for
	 * structures with fixed-length char arrays, and don't null-term.*/
	char full[17], strip[9], abbrev[5];

	memset(full, '\0', sizeof(full));
	memset(strip, '\0', sizeof(strip));
	memset(abbrev, '\0', sizeof(abbrev));

	line_fullname(full, line, sizeof(full)-1);
	line_stripname(strip, full, sizeof(strip)-1);
	line_abbrevname(abbrev, full, sizeof(abbrev)-1);
	printf("%s: %s, %s, %s\n", line, full, strip, abbrev);

} /* testLineName() */


int
testOutput()
{
	printf("**\n** Testing linename functions\n**\n");
	testLineName("/dev/pts/1");
	testLineName("pts/1");
	testLineName("pts/999");
	testLineName("/dev/ttyp00");
	testLineName("ttyp00");

	return 1;
} /* testOutput() */


/* show which options got compiled in */
void
showOptions(void)
{
	printf("**\n** Compile-time options\n**\n");

	printf("login recording methods selected:\n");
#ifdef USE_LOGIN
	printf("\tUSE_LOGIN\n");
#endif
#ifdef USE_UTMP
	printf("\tUSE_UTMP (UTMP_FILE=%s)\n", UTMP_FILE);
#endif
#ifdef USE_UTMPX
	printf("\tUSE_UTMPX\n");
#endif
#ifdef USE_WTMP
	printf("\tUSE_WTMP (WTMP_FILE=%s)\n", WTMP_FILE);
#endif
#ifdef USE_WTMPX
	printf("\tUSE_WTMPX (WTMPX_FILE=%s)\n", WTMPX_FILE);
#endif
#ifdef USE_LASTLOG
	printf("\tUSE_LASTLOG (LASTLOG_FILE=%s)\n", LASTLOG_FILE);
#endif
	printf("\n");

} /* showOptions() */


int
main(int argc, char *argv[])
{
	printf("Platform-independent login recording test driver\n");

	__progname = ssh_get_progname(argv[0]);
	if (argc == 2) {
		if (strncmp(argv[1], "-i", 3) == 0)
			compile_opts_only = 1;
		else if (strncmp(argv[1], "-v", 3) == 0)
			be_verbose=1;
	}

	if (!compile_opts_only) {
		if (be_verbose && !testOutput())
			return 1;

		if (!testAPI())
			return 1;
	}

	showOptions();

	return 0;
} /* main() */

