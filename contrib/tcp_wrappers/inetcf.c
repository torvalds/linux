/* $FreeBSD$ */
 /*
  * Routines to parse an inetd.conf or tlid.conf file. This would be a great
  * job for a PERL script.
  * 
  * Author: Wietse Venema, Eindhoven University of Technology, The Netherlands.
  */

#ifndef lint
static char sccsid[] = "@(#) inetcf.c 1.7 97/02/12 02:13:23";
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

extern int errno;
extern void exit();

#include "tcpd.h"
#include "inetcf.h"
#include "scaffold.h"

 /*
  * Network configuration files may live in unusual places. Here are some
  * guesses. Shorter names follow longer ones.
  */
char   *inet_files[] = {
    "/private/etc/inetd.conf",		/* NEXT */
    "/etc/inet/inetd.conf",		/* SYSV4 */
    "/usr/etc/inetd.conf",		/* IRIX?? */
    "/etc/inetd.conf",			/* BSD */
    "/etc/net/tlid.conf",		/* SYSV4?? */
    "/etc/saf/tlid.conf",		/* SYSV4?? */
    "/etc/tlid.conf",			/* SYSV4?? */
    0,
};

static void inet_chk();
static char *base_name();

 /*
  * Structure with everything we know about a service.
  */
struct inet_ent {
    struct inet_ent *next;
    int     type;
    char    name[1];
};

static struct inet_ent *inet_list = 0;

static char whitespace[] = " \t\r\n";

/* inet_conf - read in and examine inetd.conf (or tlid.conf) entries */

char   *inet_cfg(conf)
char   *conf;
{
    char    buf[BUFSIZ];
    FILE   *fp;
    char   *service;
    char   *protocol;
    char   *user;
    char   *path;
    char   *arg0;
    char   *arg1;
    struct tcpd_context saved_context;
    char   *percent_m();
    int     i;
    struct stat st;

    saved_context = tcpd_context;

    /*
     * The inetd.conf (or tlid.conf) information is so useful that we insist
     * on its availability. When no file is given run a series of educated
     * guesses.
     */
    if (conf != 0) {
	if ((fp = fopen(conf, "r")) == 0) {
	    fprintf(stderr, percent_m(buf, "open %s: %m\n"), conf);
	    exit(1);
	}
    } else {
	for (i = 0; inet_files[i] && (fp = fopen(inet_files[i], "r")) == 0; i++)
	     /* void */ ;
	if (fp == 0) {
	    fprintf(stderr, "Cannot find your inetd.conf or tlid.conf file.\n");
	    fprintf(stderr, "Please specify its location.\n");
	    exit(1);
	}
	conf = inet_files[i];
	check_path(conf, &st);
    }

    /*
     * Process the file. After the 7.0 wrapper release it became clear that
     * there are many more inetd.conf formats than the 8 systems that I had
     * studied. EP/IX uses a two-line specification for rpc services; HP-UX
     * permits long lines to be broken with backslash-newline.
     */
    tcpd_context.file = conf;
    tcpd_context.line = 0;
    while (xgets(buf, sizeof(buf), fp)) {
	service = strtok(buf, whitespace);	/* service */
	if (service == 0 || *service == '#')
	    continue;
	if (STR_NE(service, "stream") && STR_NE(service, "dgram"))
	    strtok((char *) 0, whitespace);	/* endpoint */
	protocol = strtok((char *) 0, whitespace);
	(void) strtok((char *) 0, whitespace);	/* wait */
	if ((user = strtok((char *) 0, whitespace)) == 0)
	    continue;
	if (user[0] == '/') {			/* user */
	    path = user;
	} else {				/* path */
	    if ((path = strtok((char *) 0, whitespace)) == 0)
		continue;
	}
	if (path[0] == '?')			/* IRIX optional service */
	    path++;
	if (STR_EQ(path, "internal"))
	    continue;
	if (path[strspn(path, "-0123456789")] == 0) {

	    /*
	     * ConvexOS puts RPC version numbers before path names. Jukka
	     * Ukkonen <ukkonen@csc.fi>.
	     */
	    if ((path = strtok((char *) 0, whitespace)) == 0)
		continue;
	}
	if ((arg0 = strtok((char *) 0, whitespace)) == 0) {
	    tcpd_warn("incomplete line");
	    continue;
	}
	if (arg0[strspn(arg0, "0123456789")] == 0) {

	    /*
	     * We're reading a tlid.conf file, the format is:
	     * 
	     * ...stuff... path arg_count arguments mod_count modules
	     */
	    if ((arg0 = strtok((char *) 0, whitespace)) == 0) {
		tcpd_warn("incomplete line");
		continue;
	    }
	}
	if ((arg1 = strtok((char *) 0, whitespace)) == 0)
	    arg1 = "";

	inet_chk(protocol, path, arg0, arg1);
    }
    fclose(fp);
    tcpd_context = saved_context;
    return (conf);
}

/* inet_chk - examine one inetd.conf (tlid.conf?) entry */

static void inet_chk(protocol, path, arg0, arg1)
char   *protocol;
char   *path;
char   *arg0;
char   *arg1;
{
    char    daemon[BUFSIZ];
    struct stat st;
    int     wrap_status = WR_MAYBE;
    char   *base_name_path = base_name(path);
    char   *tcpd_proc_name = (arg0[0] == '/' ? base_name(arg0) : arg0);

    /*
     * Always warn when the executable does not exist or when it is not
     * executable.
     */
    if (check_path(path, &st) < 0) {
	tcpd_warn("%s: not found: %m", path);
    } else if ((st.st_mode & 0100) == 0) {
	tcpd_warn("%s: not executable", path);
    }

    /*
     * Cheat on the miscd tests, nobody uses it anymore.
     */
    if (STR_EQ(base_name_path, "miscd")) {
	inet_set(arg0, WR_YES);
	return;
    }

    /*
     * While we are here...
     */
    if (STR_EQ(tcpd_proc_name, "rexd") || STR_EQ(tcpd_proc_name, "rpc.rexd"))
	tcpd_warn("%s may be an insecure service", tcpd_proc_name);

    /*
     * The tcpd program gets most of the attention.
     */
    if (STR_EQ(base_name_path, "tcpd")) {

	if (STR_EQ(tcpd_proc_name, "tcpd"))
	    tcpd_warn("%s is recursively calling itself", tcpd_proc_name);

	wrap_status = WR_YES;

	/*
	 * Check: some sites install the wrapper set-uid.
	 */
	if ((st.st_mode & 06000) != 0)
	    tcpd_warn("%s: file is set-uid or set-gid", path);

	/*
	 * Check: some sites insert tcpd in inetd.conf, instead of replacing
	 * the daemon pathname.
	 */
	if (arg0[0] == '/' && STR_EQ(tcpd_proc_name, base_name(arg1)))
	    tcpd_warn("%s inserted before %s", path, arg0);

	/*
	 * Check: make sure files exist and are executable. On some systems
	 * the network daemons are set-uid so we cannot complain. Note that
	 * tcpd takes the basename only in case of absolute pathnames.
	 */
	if (arg0[0] == '/') {			/* absolute path */
	    if (check_path(arg0, &st) < 0) {
		tcpd_warn("%s: not found: %m", arg0);
	    } else if ((st.st_mode & 0100) == 0) {
		tcpd_warn("%s: not executable", arg0);
	    }
	} else {				/* look in REAL_DAEMON_DIR */
	    sprintf(daemon, "%s/%s", REAL_DAEMON_DIR, arg0);
	    if (check_path(daemon, &st) < 0) {
		tcpd_warn("%s: not found in %s: %m",
			  arg0, REAL_DAEMON_DIR);
	    } else if ((st.st_mode & 0100) == 0) {
		tcpd_warn("%s: not executable", daemon);
	    }
	}

    } else {

	/*
	 * No tcpd program found. Perhaps they used the "simple installation"
	 * recipe. Look for a file with the same basename in REAL_DAEMON_DIR.
	 * Draw some conservative conclusions when a distinct file is found.
	 */
	sprintf(daemon, "%s/%s", REAL_DAEMON_DIR, arg0);
	if (STR_EQ(path, daemon)) {
#ifdef __FreeBSD__
	    wrap_status = WR_MAYBE;
#else
	    wrap_status = WR_NOT;
#endif
	} else if (check_path(daemon, &st) >= 0) {
	    wrap_status = WR_MAYBE;
	} else if (errno == ENOENT) {
	    wrap_status = WR_NOT;
	} else {
	    tcpd_warn("%s: file lookup: %m", daemon);
	    wrap_status = WR_MAYBE;
	}
    }

    /*
     * Alas, we cannot wrap rpc/tcp services.
     */
    if (wrap_status == WR_YES && STR_EQ(protocol, "rpc/tcp"))
	tcpd_warn("%s: cannot wrap rpc/tcp services", tcpd_proc_name);

    inet_set(tcpd_proc_name, wrap_status);
}

/* inet_set - remember service status */

void    inet_set(name, type)
char   *name;
int     type;
{
    struct inet_ent *ip =
    (struct inet_ent *) malloc(sizeof(struct inet_ent) + strlen(name));

    if (ip == 0) {
	fprintf(stderr, "out of memory\n");
	exit(1);
    }
    ip->next = inet_list;
    strcpy(ip->name, name);
    ip->type = type;
    inet_list = ip;
}

/* inet_get - look up service status */

int     inet_get(name)
char   *name;
{
    struct inet_ent *ip;

    if (inet_list == 0)
	return (WR_MAYBE);

    for (ip = inet_list; ip; ip = ip->next)
	if (STR_EQ(ip->name, name))
	    return (ip->type);

    return (-1);
}

/* base_name - compute last pathname component */

static char *base_name(path)
char   *path;
{
    char   *cp;

    if ((cp = strrchr(path, '/')) != 0)
	path = cp + 1;
    return (path);
}
