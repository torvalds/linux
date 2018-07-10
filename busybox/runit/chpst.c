/*
Copyright (c) 2001-2006, Gerrit Pape
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. The name of the author may not be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ''AS IS'' AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/* Busyboxed by Denys Vlasenko <vda.linux@googlemail.com> */

//config:config CHPST
//config:	bool "chpst (8.7 kb)"
//config:	default y
//config:	help
//config:	chpst changes the process state according to the given options, and
//config:	execs specified program.
//config:
//config:config SETUIDGID
//config:	bool "setuidgid (4.2 kb)"
//config:	default y
//config:	help
//config:	Sets soft resource limits as specified by options
//config:
//config:config ENVUIDGID
//config:	bool "envuidgid (3.6 kb)"
//config:	default y
//config:	help
//config:	Sets $UID to account's uid and $GID to account's gid
//config:
//config:config ENVDIR
//config:	bool "envdir (2.5 kb)"
//config:	default y
//config:	help
//config:	Sets various environment variables as specified by files
//config:	in the given directory
//config:
//config:config SOFTLIMIT
//config:	bool "softlimit (4.3 kb)"
//config:	default y
//config:	help
//config:	Sets soft resource limits as specified by options

//applet:IF_CHPST(    APPLET_NOEXEC(chpst,     chpst, BB_DIR_USR_BIN, BB_SUID_DROP, chpst))
//                    APPLET_NOEXEC:name       main   location        suid_type     help
//applet:IF_ENVDIR(   APPLET_NOEXEC(envdir,    chpst, BB_DIR_USR_BIN, BB_SUID_DROP, envdir))
//applet:IF_ENVUIDGID(APPLET_NOEXEC(envuidgid, chpst, BB_DIR_USR_BIN, BB_SUID_DROP, envuidgid))
//applet:IF_SETUIDGID(APPLET_NOEXEC(setuidgid, chpst, BB_DIR_USR_BIN, BB_SUID_DROP, setuidgid))
//applet:IF_SOFTLIMIT(APPLET_NOEXEC(softlimit, chpst, BB_DIR_USR_BIN, BB_SUID_DROP, softlimit))

//kbuild:lib-$(CONFIG_CHPST) += chpst.o
//kbuild:lib-$(CONFIG_ENVDIR) += chpst.o
//kbuild:lib-$(CONFIG_ENVUIDGID) += chpst.o
//kbuild:lib-$(CONFIG_SETUIDGID) += chpst.o
//kbuild:lib-$(CONFIG_SOFTLIMIT) += chpst.o

//usage:#define chpst_trivial_usage
//usage:       "[-vP012] [-u USER[:GRP]] [-U USER[:GRP]] [-e DIR]\n"
//usage:       "	[-/ DIR] [-n NICE] [-m BYTES] [-d BYTES] [-o N]\n"
//usage:       "	[-p N] [-f BYTES] [-c BYTES] PROG ARGS"
//usage:#define chpst_full_usage "\n\n"
//usage:       "Change the process state, run PROG\n"
//usage:     "\n	-u USER[:GRP]	Set uid and gid"
//usage:     "\n	-U USER[:GRP]	Set $UID and $GID in environment"
//usage:     "\n	-e DIR		Set environment variables as specified by files"
//usage:     "\n			in DIR: file=1st_line_of_file"
//usage:     "\n	-/ DIR		Chroot to DIR"
//usage:     "\n	-n NICE		Add NICE to nice value"
//usage:     "\n	-m BYTES	Same as -d BYTES -s BYTES -l BYTES"
//usage:     "\n	-d BYTES	Limit data segment"
//usage:     "\n	-o N		Limit number of open files per process"
//usage:     "\n	-p N		Limit number of processes per uid"
//usage:     "\n	-f BYTES	Limit output file sizes"
//usage:     "\n	-c BYTES	Limit core file size"
//usage:     "\n	-v		Verbose"
//usage:     "\n	-P		Create new process group"
//usage:     "\n	-0		Close stdin"
//usage:     "\n	-1		Close stdout"
//usage:     "\n	-2		Close stderr"
//usage:
//usage:#define envdir_trivial_usage
//usage:       "DIR PROG ARGS"
//usage:#define envdir_full_usage "\n\n"
//usage:       "Set various environment variables as specified by files\n"
//usage:       "in the directory DIR, run PROG"
//usage:
//usage:#define envuidgid_trivial_usage
//usage:       "USER PROG ARGS"
//usage:#define envuidgid_full_usage "\n\n"
//usage:       "Set $UID to USER's uid and $GID to USER's gid, run PROG"
//usage:
//usage:#define setuidgid_trivial_usage
//usage:       "USER PROG ARGS"
//usage:#define setuidgid_full_usage "\n\n"
//usage:       "Set uid and gid to USER's uid and gid, drop supplementary group ids,\n"
//usage:       "run PROG"
//usage:
//usage:#define softlimit_trivial_usage
//usage:       "[-a BYTES] [-m BYTES] [-d BYTES] [-s BYTES] [-l BYTES]\n"
//usage:       "	[-f BYTES] [-c BYTES] [-r BYTES] [-o N] [-p N] [-t N]\n"
//usage:       "	PROG ARGS"
//usage:#define softlimit_full_usage "\n\n"
//usage:       "Set soft resource limits, then run PROG\n"
//usage:     "\n	-a BYTES	Limit total size of all segments"
//usage:     "\n	-m BYTES	Same as -d BYTES -s BYTES -l BYTES -a BYTES"
//usage:     "\n	-d BYTES	Limit data segment"
//usage:     "\n	-s BYTES	Limit stack segment"
//usage:     "\n	-l BYTES	Limit locked memory size"
//usage:     "\n	-o N		Limit number of open files per process"
//usage:     "\n	-p N		Limit number of processes per uid"
//usage:     "\nOptions controlling file sizes:"
//usage:     "\n	-f BYTES	Limit output file sizes"
//usage:     "\n	-c BYTES	Limit core file size"
//usage:     "\nEfficiency opts:"
//usage:     "\n	-r BYTES	Limit resident set size"
//usage:     "\n	-t N		Limit CPU time, process receives"
//usage:     "\n			a SIGXCPU after N seconds"

#include "libbb.h"

/*
Five applets here: chpst, envdir, envuidgid, setuidgid, softlimit.

Only softlimit and chpst are taking options:

# common
-o N            Limit number of open files per process
-p N            Limit number of processes per uid
-m BYTES        Same as -d BYTES -s BYTES -l BYTES [-a BYTES]
-d BYTES        Limit data segment
-f BYTES        Limit output file sizes
-c BYTES        Limit core file size
# softlimit
-a BYTES        Limit total size of all segments
-s BYTES        Limit stack segment
-l BYTES        Limit locked memory size
-r BYTES        Limit resident set size
-t N            Limit CPU time
# chpst
-u USER[:GRP]   Set uid and gid
-U USER[:GRP]   Set $UID and $GID in environment
-e DIR          Set environment variables as specified by files in DIR
-/ DIR          Chroot to DIR
-n NICE         Add NICE to nice value
-v              Verbose
-P              Create new process group
-0 -1 -2        Close fd 0,1,2

Even though we accept all these options for both softlimit and chpst,
they are not to be advertised on their help texts.
We have enough problems with feature creep in other people's
software, don't want to add our own.

envdir, envuidgid, setuidgid take no options, but they reuse code which
handles -e, -U and -u.
*/

enum {
	OPT_a = (1 << 0) * ENABLE_SOFTLIMIT,
	OPT_c = (1 << 1) * (ENABLE_SOFTLIMIT || ENABLE_CHPST),
	OPT_d = (1 << 2) * (ENABLE_SOFTLIMIT || ENABLE_CHPST),
	OPT_f = (1 << 3) * (ENABLE_SOFTLIMIT || ENABLE_CHPST),
	OPT_l = (1 << 4) * ENABLE_SOFTLIMIT,
	OPT_m = (1 << 5) * (ENABLE_SOFTLIMIT || ENABLE_CHPST),
	OPT_o = (1 << 6) * (ENABLE_SOFTLIMIT || ENABLE_CHPST),
	OPT_p = (1 << 7) * (ENABLE_SOFTLIMIT || ENABLE_CHPST),
	OPT_r = (1 << 8) * ENABLE_SOFTLIMIT,
	OPT_s = (1 << 9) * ENABLE_SOFTLIMIT,
	OPT_t = (1 << 10) * ENABLE_SOFTLIMIT,
	OPT_u = (1 << 11) * (ENABLE_CHPST || ENABLE_SETUIDGID),
	OPT_U = (1 << 12) * (ENABLE_CHPST || ENABLE_ENVUIDGID),
	OPT_e = (1 << 13) * (ENABLE_CHPST || ENABLE_ENVDIR),
	OPT_root = (1 << 14) * ENABLE_CHPST,
	OPT_n = (1 << 15) * ENABLE_CHPST,
	OPT_v = (1 << 16) * ENABLE_CHPST,
	OPT_P = (1 << 17) * ENABLE_CHPST,
	OPT_0 = (1 << 18) * ENABLE_CHPST,
	OPT_1 = (1 << 19) * ENABLE_CHPST,
	OPT_2 = (1 << 20) * ENABLE_CHPST,
};

/* TODO: use recursive_action? */
static NOINLINE void edir(const char *directory_name)
{
	int wdir;
	DIR *dir;
	struct dirent *d;
	int fd;

	wdir = xopen(".", O_RDONLY | O_NDELAY);
	xchdir(directory_name);
	dir = xopendir(".");
	for (;;) {
		char buf[256];
		char *tail;
		int size;

		errno = 0;
		d = readdir(dir);
		if (!d) {
			if (errno)
				bb_perror_msg_and_die("readdir %s",
						directory_name);
			break;
		}
		if (d->d_name[0] == '.')
			continue;
		fd = open(d->d_name, O_RDONLY | O_NDELAY);
		if (fd < 0) {
			if ((errno == EISDIR) && directory_name) {
				if (option_mask32 & OPT_v)
					bb_perror_msg("warning: %s/%s is a directory",
						directory_name, d->d_name);
				continue;
			}
			bb_perror_msg_and_die("open %s/%s",
						directory_name, d->d_name);
		}
		size = full_read(fd, buf, sizeof(buf)-1);
		close(fd);
		if (size < 0)
			bb_perror_msg_and_die("read %s/%s",
					directory_name, d->d_name);
		if (size == 0) {
			unsetenv(d->d_name);
			continue;
		}
		buf[size] = '\n';
		tail = strchr(buf, '\n');
		/* skip trailing whitespace */
		while (1) {
			*tail = '\0';
			tail--;
			if (tail < buf || !isspace(*tail))
				break;
		}
		xsetenv(d->d_name, buf);
	}
	closedir(dir);
	xfchdir(wdir);
	close(wdir);
}

static void limit(int what, long l)
{
	struct rlimit r;

	/* Never fails under Linux (except if you pass it bad arguments) */
	getrlimit(what, &r);
	if ((l < 0) || (l > r.rlim_max))
		r.rlim_cur = r.rlim_max;
	else
		r.rlim_cur = l;
	if (setrlimit(what, &r) == -1)
		bb_perror_msg_and_die("setrlimit");
}

int chpst_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int chpst_main(int argc UNUSED_PARAM, char **argv)
{
	struct bb_uidgid_t ugid;
	char *set_user = set_user; /* for compiler */
	char *env_dir = env_dir;
	char *root;
	char *nicestr;
	unsigned limita;
	unsigned limitc;
	unsigned limitd;
	unsigned limitf;
	unsigned limitl;
	unsigned limitm;
	unsigned limito;
	unsigned limitp;
	unsigned limitr;
	unsigned limits;
	unsigned limitt;
	unsigned opt;

	if ((ENABLE_CHPST && applet_name[0] == 'c')
	 || (ENABLE_SOFTLIMIT && applet_name[1] == 'o')
	) {
		// FIXME: can we live with int-sized limits?
		// can we live with 40000 days?
		// if yes -> getopt converts strings to numbers for us
		opt = getopt32(argv, "^+"
			"a:+c:+d:+f:+l:+m:+o:+p:+r:+s:+t:+u:U:e:"
			IF_CHPST("/:n:vP012")
			"\0" "-1",
			&limita, &limitc, &limitd, &limitf, &limitl,
			&limitm, &limito, &limitp, &limitr, &limits, &limitt,
			&set_user, &set_user, &env_dir
			IF_CHPST(, &root, &nicestr));
		argv += optind;
		if (opt & OPT_m) { // -m means -asld
			limita = limits = limitl = limitd = limitm;
			opt |= (OPT_s | OPT_l | OPT_a | OPT_d);
		}
	} else {
		option_mask32 = opt = 0;
		argv++;
		if (!*argv)
			bb_show_usage();
	}

	// envdir?
	if (ENABLE_ENVDIR && applet_name[3] == 'd') {
		env_dir = *argv++;
		opt |= OPT_e;
	}

	// setuidgid?
	if (ENABLE_SETUIDGID && applet_name[1] == 'e') {
		set_user = *argv++;
		opt |= OPT_u;
	}

	// envuidgid?
	if (ENABLE_ENVUIDGID && applet_name[0] == 'e' && applet_name[3] == 'u') {
		set_user = *argv++;
		opt |= OPT_U;
	}

	// we must have PROG [ARGS]
	if (!*argv)
		bb_show_usage();

	// set limits
	if (opt & OPT_d) {
#ifdef RLIMIT_DATA
		limit(RLIMIT_DATA, limitd);
#else
		if (opt & OPT_v)
			bb_error_msg("system does not support RLIMIT_%s",
				"DATA");
#endif
	}
	if (opt & OPT_s) {
#ifdef RLIMIT_STACK
		limit(RLIMIT_STACK, limits);
#else
		if (opt & OPT_v)
			bb_error_msg("system does not support RLIMIT_%s",
				"STACK");
#endif
	}
	if (opt & OPT_l) {
#ifdef RLIMIT_MEMLOCK
		limit(RLIMIT_MEMLOCK, limitl);
#else
		if (opt & OPT_v)
			bb_error_msg("system does not support RLIMIT_%s",
				"MEMLOCK");
#endif
	}
	if (opt & OPT_a) {
#ifdef RLIMIT_VMEM
		limit(RLIMIT_VMEM, limita);
#else
#ifdef RLIMIT_AS
		limit(RLIMIT_AS, limita);
#else
		if (opt & OPT_v)
			bb_error_msg("system does not support RLIMIT_%s",
				"VMEM");
#endif
#endif
	}
	if (opt & OPT_o) {
#ifdef RLIMIT_NOFILE
		limit(RLIMIT_NOFILE, limito);
#else
#ifdef RLIMIT_OFILE
		limit(RLIMIT_OFILE, limito);
#else
		if (opt & OPT_v)
			bb_error_msg("system does not support RLIMIT_%s",
				"NOFILE");
#endif
#endif
	}
	if (opt & OPT_p) {
#ifdef RLIMIT_NPROC
		limit(RLIMIT_NPROC, limitp);
#else
		if (opt & OPT_v)
			bb_error_msg("system does not support RLIMIT_%s",
				"NPROC");
#endif
	}
	if (opt & OPT_f) {
#ifdef RLIMIT_FSIZE
		limit(RLIMIT_FSIZE, limitf);
#else
		if (opt & OPT_v)
			bb_error_msg("system does not support RLIMIT_%s",
				"FSIZE");
#endif
	}
	if (opt & OPT_c) {
#ifdef RLIMIT_CORE
		limit(RLIMIT_CORE, limitc);
#else
		if (opt & OPT_v)
			bb_error_msg("system does not support RLIMIT_%s",
				"CORE");
#endif
	}
	if (opt & OPT_r) {
#ifdef RLIMIT_RSS
		limit(RLIMIT_RSS, limitr);
#else
		if (opt & OPT_v)
			bb_error_msg("system does not support RLIMIT_%s",
				"RSS");
#endif
	}
	if (opt & OPT_t) {
#ifdef RLIMIT_CPU
		limit(RLIMIT_CPU, limitt);
#else
		if (opt & OPT_v)
			bb_error_msg("system does not support RLIMIT_%s",
				"CPU");
#endif
	}

	if (opt & OPT_P)
		setsid();

	if (opt & OPT_e)
		edir(env_dir);

	if (opt & (OPT_u|OPT_U))
		xget_uidgid(&ugid, set_user);

	// chrooted jail must have /etc/passwd if we move this after chroot.
	// OTOH chroot fails for non-roots.
	// Solution: cache uid/gid before chroot, apply uid/gid after.
	if (opt & OPT_U) {
		xsetenv("GID", utoa(ugid.gid));
		xsetenv("UID", utoa(ugid.uid));
	}

	if (opt & OPT_root) {
		xchroot(root);
	}

	/* nice should be done before xsetuid */
	if (opt & OPT_n) {
		errno = 0;
		if (nice(xatoi(nicestr)) == -1)
			bb_perror_msg_and_die("nice");
	}

	if (opt & OPT_u) {
		if (setgroups(1, &ugid.gid) == -1)
			bb_perror_msg_and_die("setgroups");
		xsetgid(ugid.gid);
		xsetuid(ugid.uid);
	}

	if (opt & OPT_0)
		close(STDIN_FILENO);
	if (opt & OPT_1)
		close(STDOUT_FILENO);
	if (opt & OPT_2)
		close(STDERR_FILENO);

	BB_EXECVP_or_die(argv);
}
