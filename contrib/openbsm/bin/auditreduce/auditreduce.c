/*-
 * Copyright (c) 2004-2008 Apple Inc.
 * Copyright (c) 2016 Robert N. M. Watson
 * All rights reserved.
 *
 * Portions of this software were developed by BAE Systems, the University of
 * Cambridge Computer Laboratory, and Memorial University under DARPA/AFRL
 * contract FA8650-15-C-7558 ("CADETS"), as part of the DARPA Transparent
 * Computing (TC) research program.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer. 
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution. 
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission. 
 * 
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/* 
 * Tool used to merge and select audit records from audit trail files 
 */

/*
 * XXX Currently we do not support merging of records from multiple
 * XXX audit trail files
 * XXX We assume that records are sorted chronologically - both wrt to 
 * XXX the records present within the file and between the files themselves
 */ 

#include <config/config.h>

#define	_GNU_SOURCE		/* Required for strptime() on glibc2. */

#ifdef HAVE_FULL_QUEUE_H
#include <sys/queue.h>
#else
#include <compat/queue.h>
#endif

#ifdef HAVE_CAP_ENTER
#include <sys/capsicum.h>
#include <sys/wait.h>
#endif

#include <bsm/libbsm.h>

#include <err.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sysexits.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <regex.h>
#include <errno.h>

#ifndef HAVE_STRLCPY
#include <compat/strlcpy.h>
#endif

#include "auditreduce.h"

static TAILQ_HEAD(tailhead, re_entry) re_head =
    TAILQ_HEAD_INITIALIZER(re_head);

extern char		*optarg;
extern int		 optind, optopt, opterr,optreset;

static au_mask_t	 maskp;		/* Class. */
static time_t		 p_atime;	/* Created after this time. */
static time_t		 p_btime;	/* Created before this time. */
static int		 p_auid;	/* Audit id. */ 
static int		 p_euid;	/* Effective user id. */
static int		 p_egid;	/* Effective group id. */ 
static int		 p_rgid;	/* Real group id. */ 
static int		 p_ruid;	/* Real user id. */ 
static int		 p_subid;	/* Subject id. */

/*
 * Maintain a dynamically sized array of events for -m
 */
static uint16_t		*p_evec;	/* Event type list */
static int		 p_evec_used;	/* Number of events used */
static int		 p_evec_alloc;	/* Number of events allocated */

/*
 * Following are the objects (-o option) that we can select upon.
 */
static char	*p_fileobj = NULL;
static char	*p_msgqobj = NULL;
static char	*p_pidobj = NULL;
static char	*p_semobj = NULL;
static char	*p_shmobj = NULL;
static char	*p_sockobj = NULL; 

static uint32_t opttochk = 0;

static void
parse_regexp(char *re_string)
{
	char *orig, *copy, re_error[64];
	struct re_entry *rep;
	int error, nstrs, i, len;

	copy = strdup(re_string);
	orig = copy;
	len = strlen(copy);
	for (nstrs = 0, i = 0; i < len; i++) {
		if (copy[i] == ',' && i > 0) {
			if (copy[i - 1] == '\\')
				strlcpy(&copy[i - 1], &copy[i], len);
			else {
				nstrs++;
				copy[i] = '\0';
			}
		}
	}
	TAILQ_INIT(&re_head);
	for (i = 0; i < nstrs + 1; i++) {
		rep = calloc(1, sizeof(*rep));
		if (rep == NULL) {
			(void) fprintf(stderr, "calloc: %s\n",
			    strerror(errno));
			exit(1);
		}
		if (*copy == '~') {
			copy++;
			rep->re_negate = 1;
		}
		rep->re_pattern = strdup(copy);
		error = regcomp(&rep->re_regexp, rep->re_pattern,
		    REG_EXTENDED | REG_NOSUB);
		if (error != 0) {
			regerror(error, &rep->re_regexp, re_error, 64);
			(void) fprintf(stderr, "regcomp: %s\n", re_error);
			exit(1);
		}
		TAILQ_INSERT_TAIL(&re_head, rep, re_glue);
		len = strlen(copy);
		copy += len + 1;
	}
	free(orig);
}

static void
usage(const char *msg)
{
	fprintf(stderr, "%s\n", msg);
	fprintf(stderr, "Usage: auditreduce [options] [file ...]\n");
	fprintf(stderr, "\tOptions are : \n");
	fprintf(stderr, "\t-A : all records\n");
	fprintf(stderr, "\t-a YYYYMMDD[HH[[MM[SS]]] : after date\n");
	fprintf(stderr, "\t-b YYYYMMDD[HH[[MM[SS]]] : before date\n");
	fprintf(stderr, "\t-c <flags> : matching class\n");
	fprintf(stderr, "\t-d YYYYMMDD : on date\n");
	fprintf(stderr, "\t-e <uid|name>  : effective user\n");
	fprintf(stderr, "\t-f <gid|group> : effective group\n");
	fprintf(stderr, "\t-g <gid|group> : real group\n");
	fprintf(stderr, "\t-j <pid> : subject id \n");
	fprintf(stderr, "\t-m <evno|evname> : matching event\n");
	fprintf(stderr, "\t-o objecttype=objectvalue\n");
	fprintf(stderr, "\t\t file=<pathname>\n");
	fprintf(stderr, "\t\t msgqid=<ID>\n");
	fprintf(stderr, "\t\t pid=<ID>\n");
	fprintf(stderr, "\t\t semid=<ID>\n");
	fprintf(stderr, "\t\t shmid=<ID>\n");
	fprintf(stderr, "\t-r <uid|name> : real user\n");
	fprintf(stderr, "\t-u <uid|name> : audit user\n");
	fprintf(stderr, "\t-v : select non-matching records\n");
	exit(EX_USAGE);
}

/*
 * Check if the given auid matches the selection criteria.
 */
static int
select_auid(int au)
{

	/* Check if we want to select on auid. */
	if (ISOPTSET(opttochk, OPT_u)) {
		if (au != p_auid)
			return (0);
	}
	return (1);
}

/*
 * Check if the given euid matches the selection criteria.
 */
static int
select_euid(int euser)
{

	/* Check if we want to select on euid. */
	if (ISOPTSET(opttochk, OPT_e)) {
		if (euser != p_euid)
			return (0);
	}
	return (1);
}

/*
 * Check if the given egid matches the selection criteria.
 */
static int
select_egid(int egrp)
{

	/* Check if we want to select on egid. */
	if (ISOPTSET(opttochk, OPT_f)) {
		if (egrp != p_egid)
			return (0);
	}
	return (1);
}

/*
 * Check if the given rgid matches the selection criteria.
 */
static int
select_rgid(int grp)
{

	/* Check if we want to select on rgid. */
	if (ISOPTSET(opttochk, OPT_g)) {
		if (grp != p_rgid)
			return (0);
	}
	return (1);
}

/*
 * Check if the given ruid matches the selection criteria.
 */
static int
select_ruid(int user)
{

	/* Check if we want to select on rgid. */
	if (ISOPTSET(opttochk, OPT_r)) {
		if (user != p_ruid)
			return (0);
	}
	return (1);
}

/*
 * Check if the given subject id (pid) matches the selection criteria.
 */
static int
select_subid(int subid)
{

	/* Check if we want to select on subject uid. */
	if (ISOPTSET(opttochk, OPT_j)) {
		if (subid != p_subid)
			return (0);
	}
	return (1);
}


/*
 * Check if object's pid maches the given pid.
 */ 
static int
select_pidobj(uint32_t pid) 
{

	if (ISOPTSET(opttochk, OPT_op)) {
		if (pid != (uint32_t)strtol(p_pidobj, (char **)NULL, 10))
			return (0);
	} 
	return (1);
}

/*
 * Check if the given ipc object with the given type matches the selection
 * criteria.
 */
static int
select_ipcobj(u_char type, uint32_t id, uint32_t *optchkd)
{

	if (type == AT_IPC_MSG) {
		SETOPT((*optchkd), OPT_om);
		if (ISOPTSET(opttochk, OPT_om)) {
			if (id != (uint32_t)strtol(p_msgqobj, (char **)NULL,
			    10))
				return (0);
		}
		return (1);
	} else if (type == AT_IPC_SEM) {
		SETOPT((*optchkd), OPT_ose);
		if (ISOPTSET(opttochk, OPT_ose)) {
			if (id != (uint32_t)strtol(p_semobj, (char **)NULL, 10))
				return (0);
		}
		return (1);
	} else if (type == AT_IPC_SHM) {
		SETOPT((*optchkd), OPT_osh);
		if (ISOPTSET(opttochk, OPT_osh)) {
			if (id != (uint32_t)strtol(p_shmobj, (char **)NULL, 10))
				return (0);
		}
		return (1);
	}

	/* Unknown type -- filter if *any* ipc filtering is required. */
	if (ISOPTSET(opttochk, OPT_om) || ISOPTSET(opttochk, OPT_ose)
	    || ISOPTSET(opttochk, OPT_osh))
		return (0);

	return (1);
}


/*
 * Check if the file name matches selection criteria.
 */
static int
select_filepath(char *path, uint32_t *optchkd)
{
	struct re_entry *rep;
	int match;

	SETOPT((*optchkd), OPT_of);
	match = 1;
	if (ISOPTSET(opttochk, OPT_of)) {
		match = 0;
		TAILQ_FOREACH(rep, &re_head, re_glue) {
			if (regexec(&rep->re_regexp, path, 0, NULL,
			    0) != REG_NOMATCH)
				return (!rep->re_negate);
		}
	}
	return (match);
}

/*
 * Returns 1 if the following pass the selection rules:
 *
 * before-time, 
 * after time, 
 * date, 
 * class, 
 * event 
 */
static int
select_hdr32(tokenstr_t tok, uint32_t *optchkd)
{
	uint16_t *ev;
	int match;

	SETOPT((*optchkd), (OPT_A | OPT_a | OPT_b | OPT_c | OPT_m | OPT_v));

	/* The A option overrides a, b and d. */
	if (!ISOPTSET(opttochk, OPT_A)) {
		if (ISOPTSET(opttochk, OPT_a)) {
			if (difftime((time_t)tok.tt.hdr32.s, p_atime) < 0) {
				/* Record was created before p_atime. */
				return (0);
			}
		}

		if (ISOPTSET(opttochk, OPT_b)) {
			if (difftime(p_btime, (time_t)tok.tt.hdr32.s) < 0) {
				/* Record was created after p_btime. */
				return (0);
			}
		}
	}

	if (ISOPTSET(opttochk, OPT_c)) {
		/*
		 * Check if the classes represented by the event matches
		 * given class.
		 */
		if (au_preselect(tok.tt.hdr32.e_type, &maskp, AU_PRS_BOTH,
		    AU_PRS_USECACHE) != 1)
			return (0);
	}

	/* Check if event matches. */
	if (ISOPTSET(opttochk, OPT_m)) {
		match = 0;
		for (ev = p_evec; ev < &p_evec[p_evec_used]; ev++)
			if (tok.tt.hdr32.e_type == *ev)
				match = 1;
		if (match == 0)
			return (0);
	}
		
	return (1);
}

static int
select_return32(tokenstr_t tok_ret32, tokenstr_t tok_hdr32, uint32_t *optchkd)
{
	int sorf;

	SETOPT((*optchkd), (OPT_c));
	if (tok_ret32.tt.ret32.status == 0)
		sorf = AU_PRS_SUCCESS;
	else
		sorf = AU_PRS_FAILURE;
	if (ISOPTSET(opttochk, OPT_c)) {
		if (au_preselect(tok_hdr32.tt.hdr32.e_type, &maskp, sorf,
		    AU_PRS_USECACHE) != 1)
			return (0);
	}
	return (1);
}

/*
 * Return 1 if checks for the the following succeed
 * auid, 
 * euid, 
 * egid, 
 * rgid, 
 * ruid, 
 * process id
 */
static int
select_proc32(tokenstr_t tok, uint32_t *optchkd)
{

	SETOPT((*optchkd), (OPT_u | OPT_e | OPT_f | OPT_g | OPT_r | OPT_op));

	if (!select_auid(tok.tt.proc32.auid))
		return (0);
	if (!select_euid(tok.tt.proc32.euid))
		return (0);
	if (!select_egid(tok.tt.proc32.egid))
		return (0);
	if (!select_rgid(tok.tt.proc32.rgid))
		return (0);
	if (!select_ruid(tok.tt.proc32.ruid))
		return (0);
	if (!select_pidobj(tok.tt.proc32.pid))
		return (0);
	return (1);
}

/*
 * Return 1 if checks for the the following succeed
 * auid, 
 * euid, 
 * egid, 
 * rgid, 
 * ruid, 
 * subject id
 */
static int
select_subj32(tokenstr_t tok, uint32_t *optchkd)
{

	SETOPT((*optchkd), (OPT_u | OPT_e | OPT_f | OPT_g | OPT_r | OPT_j));

	if (!select_auid(tok.tt.subj32.auid))
		return (0);
	if (!select_euid(tok.tt.subj32.euid))
		return (0);
	if (!select_egid(tok.tt.subj32.egid))
		return (0);
	if (!select_rgid(tok.tt.subj32.rgid))
		return (0);
	if (!select_ruid(tok.tt.subj32.ruid))
		return (0);
	if (!select_subid(tok.tt.subj32.pid))
		return (0);
	return (1);
}

/*
 * Read each record from the audit trail.  Check if it is selected after
 * passing through each of the options 
 */
static int
select_records(FILE *fp)
{
	tokenstr_t tok_hdr32_copy;
	u_char *buf;
	tokenstr_t tok;
	int reclen;
	int bytesread;
	int selected;
	uint32_t optchkd;
	int print;

	int err = 0;
	while ((reclen = au_read_rec(fp, &buf)) != -1) {
		optchkd = 0;
		bytesread = 0;
		selected = 1;
		while ((selected == 1) && (bytesread < reclen)) {
			if (-1 == au_fetch_tok(&tok, buf + bytesread,
			    reclen - bytesread)) {
				/* Is this an incomplete record? */
				err = 1;
				break;
			}

			/*
			 * For each token type we have have different
			 * selection criteria.
			 */
			switch(tok.id) {
			case AUT_HEADER32:
					selected = select_hdr32(tok,
					    &optchkd);
					bcopy(&tok, &tok_hdr32_copy,
					    sizeof(tok));
					break;

			case AUT_PROCESS32:
					selected = select_proc32(tok,
					    &optchkd);
					break;

			case AUT_SUBJECT32:
					selected = select_subj32(tok,
					    &optchkd);
					break;

			case AUT_IPC:
					selected = select_ipcobj(
					    tok.tt.ipc.type, tok.tt.ipc.id,
					    &optchkd); 
					break;

			case AUT_PATH:
					selected = select_filepath(
					    tok.tt.path.path, &optchkd);
					break;	

			case AUT_RETURN32:
				selected = select_return32(tok,
				    tok_hdr32_copy, &optchkd);
				break;

			default:
				break;
			}
			bytesread += tok.len;
		}
		/* Check if all the options were matched. */
		print = ((selected == 1) && (!err) && (!(opttochk & ~optchkd)));
		if (ISOPTSET(opttochk, OPT_v))
			print = !print;
		if (print)
			(void) fwrite(buf, 1, reclen, stdout);
		free(buf);
	}
	return (0);
}

/* 
 * The -o option has the form object_type=object_value.  Identify the object
 * components.
 */
static void
parse_object_type(char *name, char *val)
{
	if (val == NULL)
		return;

	if (!strcmp(name, FILEOBJ)) {
		p_fileobj = val;
		parse_regexp(val);
		SETOPT(opttochk, OPT_of);
	} else if (!strcmp(name, MSGQIDOBJ)) {
		p_msgqobj = val;
		SETOPT(opttochk, OPT_om);
	} else if (!strcmp(name, PIDOBJ)) {
		p_pidobj = val;
		SETOPT(opttochk, OPT_op);
	} else if (!strcmp(name, SEMIDOBJ)) {
		p_semobj = val;
		SETOPT(opttochk, OPT_ose);
	} else if (!strcmp(name, SHMIDOBJ)) {
		p_shmobj = val;
		SETOPT(opttochk, OPT_osh);
	} else if (!strcmp(name, SOCKOBJ)) {
		p_sockobj = val;
		SETOPT(opttochk, OPT_oso);
	} else
		usage("unknown value for -o");
}

int
main(int argc, char **argv)
{
	struct group *grp;
	struct passwd *pw;
	struct tm tm;
	au_event_t *n;
	FILE *fp;
	int i;
	char *objval, *converr;
	int ch;
	char timestr[128];
	char *fname;
	uint16_t *etp;
#ifdef HAVE_CAP_ENTER
	int retval, status;
	pid_t childpid, pid;
#endif

	converr = NULL;

	while ((ch = getopt(argc, argv, "Aa:b:c:d:e:f:g:j:m:o:r:u:v")) != -1) {
		switch(ch) {
		case 'A':
			SETOPT(opttochk, OPT_A);
			break;

		case 'a':
			if (ISOPTSET(opttochk, OPT_a)) {
				usage("d is exclusive with a and b");
			}
			SETOPT(opttochk, OPT_a);
			bzero(&tm, sizeof(tm));
			strptime(optarg, "%Y%m%d%H%M%S", &tm);
			strftime(timestr, sizeof(timestr), "%Y%m%d%H%M%S",
			    &tm);
			/* fprintf(stderr, "Time converted = %s\n", timestr); */
			p_atime = mktime(&tm);
			break; 	

		case 'b':
			if (ISOPTSET(opttochk, OPT_b)) {
				usage("d is exclusive with a and b");
			}
			SETOPT(opttochk, OPT_b);
			bzero(&tm, sizeof(tm));
			strptime(optarg, "%Y%m%d%H%M%S", &tm);
			strftime(timestr, sizeof(timestr), "%Y%m%d%H%M%S",
			    &tm);
			/* fprintf(stderr, "Time converted = %s\n", timestr); */
			p_btime = mktime(&tm);
			break; 	

		case 'c':
			if (0 != getauditflagsbin(optarg, &maskp)) {
				/* Incorrect class */
				usage("Incorrect class");
			}
			SETOPT(opttochk, OPT_c);
			break;

		case 'd':
			if (ISOPTSET(opttochk, OPT_b) || ISOPTSET(opttochk,
			    OPT_a))
				usage("'d' is exclusive with 'a' and 'b'");
			SETOPT(opttochk, OPT_d);
			bzero(&tm, sizeof(tm));
			strptime(optarg, "%Y%m%d", &tm);
			strftime(timestr, sizeof(timestr), "%Y%m%d", &tm);
			/* fprintf(stderr, "Time converted = %s\n", timestr); */
			p_atime = mktime(&tm);
			tm.tm_hour = 23;
			tm.tm_min = 59;
			tm.tm_sec = 59;
			strftime(timestr, sizeof(timestr), "%Y%m%d", &tm);
			/* fprintf(stderr, "Time converted = %s\n", timestr); */
			p_btime = mktime(&tm);
			break;

		case 'e':
			p_euid = strtol(optarg, &converr, 10);
			if (*converr != '\0') {
				/* Try the actual name */
				if ((pw = getpwnam(optarg)) == NULL)
					break;
				p_euid = pw->pw_uid;
			}
			SETOPT(opttochk, OPT_e);
			break;

		case 'f':
			p_egid = strtol(optarg, &converr, 10);
			if (*converr != '\0') {
				/* Try actual group name. */
				if ((grp = getgrnam(optarg)) == NULL)
					break;
				p_egid = grp->gr_gid;
			}
			SETOPT(opttochk, OPT_f);
			break;

		case 'g':
			p_rgid = strtol(optarg, &converr, 10);
			if (*converr != '\0') {
				/* Try actual group name. */
				if ((grp = getgrnam(optarg)) == NULL) 
					break;
				p_rgid = grp->gr_gid;
			}
			SETOPT(opttochk, OPT_g);
			break;

		case 'j':
			p_subid = strtol(optarg, (char **)NULL, 10);
			SETOPT(opttochk, OPT_j);
			break;

		case 'm':
			if (p_evec == NULL) {
				p_evec_alloc = 32;
				p_evec = malloc(sizeof(*etp) * p_evec_alloc);
				if (p_evec == NULL)
					err(1, "malloc");
			} else if (p_evec_alloc == p_evec_used) {
				p_evec_alloc <<= 1;
				p_evec = realloc(p_evec,
				    sizeof(*p_evec) * p_evec_alloc);
				if (p_evec == NULL)
					err(1, "realloc");
			}
			etp = &p_evec[p_evec_used++];
			*etp = strtol(optarg, (char **)NULL, 10);
			if (*etp == 0) {
				/* Could be the string representation. */
				n = getauevnonam(optarg);
				if (n == NULL)
					usage("Incorrect event name");
				*etp = *n;
			}
			SETOPT(opttochk, OPT_m);
			break;

		case 'o':
			objval = strchr(optarg, '=');
			if (objval != NULL) {
				*objval = '\0';
				objval += 1;			
				parse_object_type(optarg, objval);
			}
			break;

		case 'r':
			p_ruid = strtol(optarg, &converr, 10);
			if (*converr != '\0') {
				if ((pw = getpwnam(optarg)) == NULL)
					break;
				p_ruid = pw->pw_uid;
			}
			SETOPT(opttochk, OPT_r);
			break;

		case 'u':
			p_auid = strtol(optarg, &converr, 10);
			if (*converr != '\0') {
				if ((pw = getpwnam(optarg)) == NULL)
					break;
				p_auid = pw->pw_uid;
			}
			SETOPT(opttochk, OPT_u);
			break;

		case 'v':
			SETOPT(opttochk, OPT_v);
			break;

		case '?':
		default:
			usage("Unknown option");
		}
	}
	argv += optind;
	argc -= optind;

	if (argc == 0) {
#ifdef HAVE_CAP_ENTER
		retval = cap_enter();
		if (retval != 0 && errno != ENOSYS)
			err(EXIT_FAILURE, "cap_enter");
#endif
		if (select_records(stdin) == -1)
			errx(EXIT_FAILURE,
			    "Couldn't select records from stdin");
		exit(EXIT_SUCCESS);
	}

	/*
	 * XXX: We should actually be merging records here.
	 */
	for (i = 0; i < argc; i++) {
		fname = argv[i];
		fp = fopen(fname, "r");
		if (fp == NULL)
			errx(EXIT_FAILURE, "Couldn't open %s", fname);

		/*
		 * If operating with sandboxing, create a sandbox process for
		 * each trail file we operate on.  This avoids the need to do
		 * fancy things with file descriptors, etc, when iterating on
		 * a list of arguments.
		 *
		 * NB: Unlike praudit(1), auditreduce(1) terminates if it hits
		 * any errors.  Propagate the error from the child to the
		 * parent if any problems arise.
		 */
#ifdef HAVE_CAP_ENTER
		childpid = fork();
		if (childpid == 0) {
			/* Child. */
			retval = cap_enter();
			if (retval != 0 && errno != ENOSYS)
				errx(EXIT_FAILURE, "cap_enter");
			if (select_records(fp) == -1)
				errx(EXIT_FAILURE,
				    "Couldn't select records %s", fname);
			exit(0);
		}

		/* Parent.  Await child termination, check exit value. */
		while ((pid = waitpid(childpid, &status, 0)) != childpid);
		if (WEXITSTATUS(status) != 0)
			exit(EXIT_FAILURE);
#else
		if (select_records(fp) == -1)
			errx(EXIT_FAILURE, "Couldn't select records %s",
			    fname);
#endif
		fclose(fp);
	}
	exit(EXIT_SUCCESS);
}
