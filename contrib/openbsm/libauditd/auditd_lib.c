/*-
 * Copyright (c) 2008-2009 Apple Inc.
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

#include <sys/param.h>

#include <config/config.h>

#include <sys/dirent.h>
#ifdef HAVE_FULL_QUEUE_H
#include <sys/queue.h>
#else /* !HAVE_FULL_QUEUE_H */
#include <compat/queue.h>
#endif /* !HAVE_FULL_QUEUE_H */
#include <sys/mount.h>
#include <sys/socket.h>

#include <sys/stat.h>
#include <sys/time.h>

#include <netinet/in.h>

#include <bsm/audit.h>
#include <bsm/audit_uevents.h>
#include <bsm/auditd_lib.h>
#include <bsm/libbsm.h>

#include <assert.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <netdb.h>

#ifdef __APPLE__
#include <notify.h>
#ifndef __BSM_INTERNAL_NOTIFY_KEY
#define	__BSM_INTERNAL_NOTIFY_KEY	"com.apple.audit.change"
#endif /* __BSM_INTERNAL_NOTIFY_KEY */
#endif /* __APPLE__ */

/*
 * XXX This is temporary until this is moved to <bsm/audit.h> and shared with
 * the kernel.
 */
#ifndef	AUDIT_HARD_LIMIT_FREE_BLOCKS
#define	AUDIT_HARD_LIMIT_FREE_BLOCKS	4
#endif

/*
 * Number of seconds to January 1, 2000
 */
#define	JAN_01_2000	946598400

struct dir_ent {
	char			*dirname;
	uint8_t			 softlim;
	uint8_t			 hardlim;
	TAILQ_ENTRY(dir_ent)	 dirs;
};

static TAILQ_HEAD(, dir_ent)	dir_q;

struct audit_trail {
	time_t			 at_time;
	char			*at_path;
	off_t			 at_size;

	TAILQ_ENTRY(audit_trail) at_trls;
};

static int auditd_minval = -1;
static int auditd_dist = 0;

static char auditd_host[MAXHOSTNAMELEN];
static int auditd_hostlen = -1;

static char *auditd_errmsg[] = {
	"no error",					/* ADE_NOERR	( 0) */
	"could not parse audit_control(5) file",	/* ADE_PARSE	( 1) */
	"auditon(2) failed",				/* ADE_AUDITON	( 2) */
	"malloc(3) failed",				/* ADE_NOMEM	( 3) */
	"all audit log directories over soft limit",	/* ADE_SOFTLIM	( 4) */
	"all audit log directories over hard limit",	/* ADE_HARDLIM	( 5) */
	"could not create file name string",		/* ADE_STRERR	( 6) */
	"could not open audit record",			/* ADE_AU_OPEN	( 7) */
	"could not close audit record",			/* ADE_AU_CLOSE	( 8) */
	"could not set active audit session state",	/* ADE_SETAUDIT	( 9) */
	"auditctl(2) failed (trail still swapped)",	/* ADE_ACTL	(10) */
	"auditctl(2) failed (trail not swapped)",	/* ADE_ACTLERR	(11) */
	"could not swap audit trail file",		/* ADE_SWAPERR	(12) */
	"could not rename crash recovery file",		/* ADE_RENAME	(13) */
	"could not read 'current' link file",		/* ADE_READLINK	(14) */
	"could not create 'current' link file",		/* ADE_SYMLINK	(15) */
	"invalid argument",				/* ADE_INVAL	(16) */
	"could not resolve hostname to address",	/* ADE_GETADDR	(17) */
	"address family not supported",			/* ADE_ADDRFAM	(18) */
	"error expiring audit trail files",		/* ADE_EXPIRE	(19) */
};

#define	MAXERRCODE	(sizeof(auditd_errmsg) / sizeof(auditd_errmsg[0]))

#define	NA_EVENT_STR_SIZE	128
#define	POL_STR_SIZE		128


/*
 * Look up and return the error string for the given audit error code.
 */
const char *
auditd_strerror(int errcode)
{
	int idx = -errcode;

	if (idx < 0 || idx > (int)MAXERRCODE)
		return ("Invalid auditd error code");

	return (auditd_errmsg[idx]);
}


/*
 * Free our local list of directory names and init list.
 */
static void
free_dir_q(void)
{
	struct dir_ent *d1, *d2;

	d1 = TAILQ_FIRST(&dir_q);
	while (d1 != NULL) {
		d2 = TAILQ_NEXT(d1, dirs);
		free(d1->dirname);
		free(d1);
		d1 = d2;
	}
	TAILQ_INIT(&dir_q);
}

/*
 * Concat the directory name to the given file name.
 * XXX We should affix the hostname also
 */
static char *
affixdir(char *name, struct dir_ent *dirent)
{
	char *fn = NULL;

	/*
	 * Sanity check on file name.
	 */
	if (strlen(name) != FILENAME_LEN) {
		errno = EINVAL;
		return (NULL);
	}

	/*
	 * If the host is set then also add the hostname to the filename.
	 */
	if (auditd_hostlen > 0)
		asprintf(&fn, "%s/%s.%s", dirent->dirname, name, auditd_host);
	else
		asprintf(&fn, "%s/%s", dirent->dirname, name);
	return (fn);
}

/*
 * Insert the directory entry in the list by the way they are ordered in
 * audit_control(5).  Move the entries that are over the soft and hard limits
 * toward the tail.
 */
static void
insert_orderly(struct dir_ent *denew)
{
	struct dir_ent *dep;

	TAILQ_FOREACH(dep, &dir_q, dirs) {
		if (dep->softlim == 1 && denew->softlim == 0) {
			TAILQ_INSERT_BEFORE(dep, denew, dirs);
			return;
		}
		if (dep->hardlim == 1 && denew->hardlim == 0) {
			TAILQ_INSERT_BEFORE(dep, denew, dirs);
			return;
		}
	}
	TAILQ_INSERT_TAIL(&dir_q, denew, dirs);
}

/*
 * Get the min percentage of free blocks from audit_control(5) and that
 * value in the kernel.  Return:
 *	ADE_NOERR	on success,
 *	ADE_PARSE	error parsing audit_control(5),
 */
int
auditd_set_dist(void)
{
	int ret;

	ret = getacdist();
	if (ret < 0)
		return (ADE_PARSE);

	auditd_dist = ret;

	return (ADE_NOERR);
}

/*
 * Get the host from audit_control(5) and set it in the audit kernel
 * information.  Return:
 *	ADE_NOERR	on success.
 *	ADE_PARSE	error parsing audit_control(5).
 *	ADE_AUDITON	error getting/setting auditon(2) value.
 *	ADE_GETADDR	error getting address info for host.
 *	ADE_ADDRFAM	un-supported address family.
 */
int
auditd_set_host(void)
{
	struct sockaddr_in6 *sin6;
	struct sockaddr_in *sin;
	struct addrinfo *res;
	struct auditinfo_addr aia;
	int error, ret = ADE_NOERR;

	if ((getachost(auditd_host, sizeof(auditd_host)) != 0) ||
	    ((auditd_hostlen = strlen(auditd_host)) == 0)) {
		ret = ADE_PARSE;

		/*
		 * To maintain reverse compatability with older audit_control
		 * files, simply drop a warning if the host parameter has not
		 * been set.  However, we will explicitly disable the
		 * generation of extended audit header by passing in a zeroed
		 * termid structure.
		 */
		bzero(&aia, sizeof(aia));
		aia.ai_termid.at_type = AU_IPv4;
		error = audit_set_kaudit(&aia, sizeof(aia));
		if (error < 0 && errno != ENOSYS)
			ret = ADE_AUDITON;
		return (ret);
	}
	error = getaddrinfo(auditd_host, NULL, NULL, &res);
	if (error)
		return (ADE_GETADDR);
	switch (res->ai_family) {
	case PF_INET6:
		sin6 = (struct sockaddr_in6 *) res->ai_addr;
		bcopy(&sin6->sin6_addr.s6_addr,
		    &aia.ai_termid.at_addr[0], sizeof(struct in6_addr));
		aia.ai_termid.at_type = AU_IPv6;
		break;

	case PF_INET:
		sin = (struct sockaddr_in *) res->ai_addr;
		bcopy(&sin->sin_addr.s_addr,
		    &aia.ai_termid.at_addr[0], sizeof(struct in_addr));
		aia.ai_termid.at_type = AU_IPv4;
		break;

	default:
		/* Un-supported address family in host parameter. */
		errno = EAFNOSUPPORT;
		return (ADE_ADDRFAM);
	}

	if (audit_set_kaudit(&aia, sizeof(aia)) < 0)
		ret = ADE_AUDITON;

	return (ret);
}

/*
 * Get the min percentage of free blocks from audit_control(5) and that
 * value in the kernel.  Return:
 *	ADE_NOERR	on success,
 *	ADE_PARSE	error parsing audit_control(5),
 *	ADE_AUDITON	error getting/setting auditon(2) value.
 */
int
auditd_set_minfree(void)
{
	au_qctrl_t qctrl;

	if (getacmin(&auditd_minval) != 0)
		return (ADE_PARSE);

	if (audit_get_qctrl(&qctrl, sizeof(qctrl)) != 0)
		return (ADE_AUDITON);

	if (qctrl.aq_minfree != auditd_minval) {
		qctrl.aq_minfree = auditd_minval;
		if (audit_set_qctrl(&qctrl, sizeof(qctrl)) != 0)
			return (ADE_AUDITON);
	}

	return (0);
}

/*
 * Convert a trailname into a timestamp (seconds).  Return 0 if the conversion
 * was successful.
 */
static int
trailname_to_tstamp(char *fn, time_t *tstamp)
{
	struct tm tm;
	char ts[TIMESTAMP_LEN + 1];
	char *p;

	*tstamp = 0;

	/*
	 * Get the ending time stamp.
	 */
	if ((p = strchr(fn, '.')) == NULL)
		return (1);
	strlcpy(ts, ++p, sizeof(ts));
	if (strlen(ts) != POSTFIX_LEN)
		return (1);

	bzero(&tm, sizeof(tm));

	/* seconds (0-60) */
	p = ts + POSTFIX_LEN - 2;
	tm.tm_sec = atol(p);
	if (tm.tm_sec < 0 || tm.tm_sec > 60)
		return (1);

	/* minutes (0-59) */
	*p = '\0'; p -= 2;
	tm.tm_min = atol(p);
	if (tm.tm_min < 0 || tm.tm_min > 59)
		return (1);

	/* hours (0 - 23) */
	*p = '\0'; p -= 2;
	tm.tm_hour = atol(p);
	if (tm.tm_hour < 0 || tm.tm_hour > 23)
		return (1);

	/* day of month (1-31) */
	*p = '\0'; p -= 2;
	tm.tm_mday = atol(p);
	if (tm.tm_mday < 1 || tm.tm_mday > 31)
		return (1);

	/* month (0 - 11) */
	*p = '\0'; p -= 2;
	tm.tm_mon = atol(p) - 1;
	if (tm.tm_mon < 0 || tm.tm_mon > 11)
		return (1);

	/* year (year - 1900) */
	*p = '\0'; p -= 4;
	tm.tm_year = atol(p) - 1900;
	if (tm.tm_year < 0)
		return (1);

	*tstamp = timegm(&tm);

	return (0);
}

/*
 * Remove audit trails files according to the expiration conditions.  Returns:
 *	ADE_NOERR	on success or there is nothing to do.
 *	ADE_PARSE	if error parsing audit_control(5).
 *	ADE_NOMEM	if could not allocate memory.
 *	ADE_READLINK	if could not read link file.
 *	ADE_EXPIRE	if there was an unexpected error.
 */
int
auditd_expire_trails(int (*warn_expired)(char *))
{
	int andflg, len, ret = ADE_NOERR;
	size_t expire_size, total_size = 0L;
	time_t expire_age, oldest_time, current_time = time(NULL);
	struct dir_ent *traildir;
	struct audit_trail *at;
	char *afnp, *pn;
	TAILQ_HEAD(au_trls_head, audit_trail) head =
	    TAILQ_HEAD_INITIALIZER(head);
	struct stat stbuf;
	char activefn[MAXPATHLEN];

	/*
	 * Read the expiration conditions.  If no conditions then return no
	 * error.
	 */
	if (getacexpire(&andflg, &expire_age, &expire_size) < 0)
		return (ADE_PARSE);
	if (!expire_age && !expire_size)
		return (ADE_NOERR);

	/*
	 * Read the 'current' trail file name.  Trim off directory path.
	 */
	activefn[0] = '\0';
	len = readlink(AUDIT_CURRENT_LINK, activefn, MAXPATHLEN - 1);
	if (len < 0)
		return (ADE_READLINK);
	if ((afnp = strrchr(activefn, '/')) != NULL)
		afnp++;


	/*
	 * Build tail queue of the trail files.
	 */
	TAILQ_FOREACH(traildir, &dir_q, dirs) {
		DIR *dirp;
		struct dirent *dp;

		dirp = opendir(traildir->dirname);
		while ((dp = readdir(dirp)) != NULL) {
			time_t tstamp = 0;
			struct audit_trail *new;

			/*
			 * Quickly filter non-trail files.
			 */
			if (dp->d_namlen < FILENAME_LEN ||
			    dp->d_name[POSTFIX_LEN] != '.')
				continue;

			if (asprintf(&pn, "%s/%s", traildir->dirname,
			    dp->d_name) < 0) {
				ret = ADE_NOMEM;
				break;
			}

			if (stat(pn, &stbuf) < 0 || !S_ISREG(stbuf.st_mode)) {
				free(pn);
				continue;
			}

			total_size += stbuf.st_size;

			/*
			 * If this is the 'current' audit trail then
			 * don't add it to the tail queue.
			 */
			if (NULL != afnp && strcmp(dp->d_name, afnp) == 0) {
				free(pn);
				continue;
			}

			/*
			 * Get the ending time stamp encoded in the trail
			 * name.  If we can't read it or if it is older
			 * than Jan 1, 2000 then use the mtime.
			 */
			if (trailname_to_tstamp(dp->d_name, &tstamp) != 0 ||
			    tstamp < JAN_01_2000)
				tstamp = stbuf.st_mtime;

			/*
			 * If the time stamp is older than Jan 1, 2000 then
			 * update the mtime of the trail file to the current
			 * time. This is so we don't prematurely remove a trail
			 * file that was created while the system clock reset
			 * to the * "beginning of time" but later the system
			 * clock is set to the correct current time.
			 */
			if (current_time >= JAN_01_2000 &&
			    tstamp < JAN_01_2000) {
				struct timeval tv[2];

				tstamp = stbuf.st_mtime = current_time;
				TIMESPEC_TO_TIMEVAL(&tv[0],
				    &stbuf.st_atimespec);
				TIMESPEC_TO_TIMEVAL(&tv[1],
				    &stbuf.st_mtimespec);
				utimes(pn, tv);
			}

			/*
			 * Allocate and populate the new entry.
			 */
			new = malloc(sizeof(*new));
			if (NULL == new) {
				free(pn);
				ret = ADE_NOMEM;
				break;
			}
			new->at_time = tstamp;
			new->at_size = stbuf.st_size;
			new->at_path = pn;

			/*
			 * Check to see if we have a new head.  Otherwise,
			 * walk the tailq from the tail first and do a simple
			 * insertion sort.
			 */
			if (TAILQ_EMPTY(&head) ||
			    new->at_time <= TAILQ_FIRST(&head)->at_time) {
				TAILQ_INSERT_HEAD(&head, new, at_trls);
				continue;
			}

			TAILQ_FOREACH_REVERSE(at, &head, au_trls_head, at_trls)
				if (new->at_time >= at->at_time) {
					TAILQ_INSERT_AFTER(&head, at, new,
					    at_trls);
					break;
				}

		}
		closedir(dirp);
	}

	oldest_time = current_time - expire_age;

	/*
	 * Expire trail files, oldest (mtime) first, if the given
	 * conditions are met.
	 */
	at = TAILQ_FIRST(&head);
	while (NULL != at) {
		struct audit_trail *at_next = TAILQ_NEXT(at, at_trls);

		if (andflg) {
			if ((expire_size && total_size > expire_size) &&
			    (expire_age && at->at_time < oldest_time)) {
				if (warn_expired)
					(*warn_expired)(at->at_path);
				if (unlink(at->at_path) < 0)
					ret = ADE_EXPIRE;
				total_size -= at->at_size;
			}
		} else {
			if ((expire_size && total_size > expire_size) ||
			    (expire_age && at->at_time < oldest_time)) {
				if (warn_expired)
					(*warn_expired)(at->at_path);
				if (unlink(at->at_path) < 0)
					ret = ADE_EXPIRE;
				total_size -= at->at_size;
			}
		}

		free(at->at_path);
		free(at);
		at = at_next;
	}

	return (ret);
}

/*
 * Parses the "dir" entry in audit_control(5) into an ordered list.  Also, will
 * set the minfree and host values if not already set.  Arguments include
 * function pointers to audit_warn functions for soft and hard limits. Returns:
 *	ADE_NOERR	on success,
 *	ADE_PARSE	error parsing audit_control(5),
 *	ADE_AUDITON	error getting/setting auditon(2) value,
 *	ADE_NOMEM	error allocating memory,
 *	ADE_SOFTLIM	if all the directories are over the soft limit,
 *	ADE_HARDLIM	if all the directories are over the hard limit,
 */
int
auditd_read_dirs(int (*warn_soft)(char *), int (*warn_hard)(char *))
{
	char cur_dir[MAXNAMLEN];
	struct dir_ent *dirent;
	struct statfs sfs;
	int err;
	char soft, hard;
	int tcnt = 0;
	int scnt = 0;
	int hcnt = 0;

	if (auditd_minval == -1 && (err = auditd_set_minfree()) != 0)
		return (err);

	if (auditd_hostlen == -1)
		auditd_set_host();

	/*
	 * Init directory q.  Force a re-read of the file the next time.
	 */
	free_dir_q();
	endac();

	/*
	 * Read the list of directories into an ordered linked list
	 * admin's preference, then those over soft limit and, finally,
	 * those over the hard limit.
	 *
	 * XXX We should use the reentrant interfaces once they are
	 * available.
	 */
	while (getacdir(cur_dir, MAXNAMLEN) >= 0) {
		if (statfs(cur_dir, &sfs) < 0)
			continue;  /* XXX should warn */
		soft = (sfs.f_bfree < (sfs.f_blocks * auditd_minval / 100 )) ?
		    1 : 0;
		hard = (sfs.f_bfree < AUDIT_HARD_LIMIT_FREE_BLOCKS) ? 1 : 0;
		if (soft) {
			if (warn_soft)
				(*warn_soft)(cur_dir);
			scnt++;
		}
		if (hard) {
			if (warn_hard)
				(*warn_hard)(cur_dir);
			hcnt++;
		}
		dirent = (struct dir_ent *) malloc(sizeof(struct dir_ent));
		if (dirent == NULL)
			return (ADE_NOMEM);
		dirent->softlim = soft;
		dirent->hardlim = hard;
		dirent->dirname = (char *) malloc(MAXNAMLEN);
		if (dirent->dirname == NULL) {
			free(dirent);
			return (ADE_NOMEM);
		}
		strlcpy(dirent->dirname, cur_dir, MAXNAMLEN);
		insert_orderly(dirent);
		tcnt++;
	}

	if (hcnt == tcnt)
		return (ADE_HARDLIM);
	if (scnt == tcnt)
		return (ADE_SOFTLIM);
	return (0);
}

void
auditd_close_dirs(void)
{
	free_dir_q();
	auditd_minval = -1;
	auditd_hostlen = -1;
}


/*
 * Process the audit event file, obtaining a class mapping for each event, and
 * set that mapping into the kernel. Return:
 *	 n	number of event mappings that were successfully processed,
 *   ADE_NOMEM	if there was an error allocating memory.
 *
 * Historically, this code only set up the in-kernel class mapping.  On
 * systems with an in-kernel event-to-name mapping, it also now installs that,
 * as it is iterating over the event list anyway.  Failures there will be
 * ignored as not all kernels support the feature.
 */
int
auditd_set_evcmap(void)
{
	au_event_ent_t ev, *evp;
	au_evclass_map_t evc_map;
	au_evname_map_t evn_map;
	int ctr = 0;

	/*
	 * XXX There's a risk here that the BSM library will return NULL
	 * for an event when it can't properly map it to a class. In that
	 * case, we will not process any events beyond the one that failed,
	 * but should. We need a way to get a count of the events.
	 */
	ev.ae_name = (char *)malloc(AU_EVENT_NAME_MAX);
	ev.ae_desc = (char *)malloc(AU_EVENT_DESC_MAX);
	if (ev.ae_name == NULL || ev.ae_desc == NULL) {
		if (ev.ae_name != NULL)
			free(ev.ae_name);
		return (ADE_NOMEM);
	}

	/*
	 * XXXRW: Currently we have no way to remove mappings from the kernel
	 * when they are removed from the file-based mappings.
	 */
	evp = &ev;
	setauevent();
	while ((evp = getauevent_r(evp)) != NULL) {
		/*
		 * Set the event-to-name mapping entry.  If there's not room
		 * in the in-kernel string, then we skip the entry.  Possibly
		 * better than truncating...?
		 */
		if (strlcpy(evn_map.en_name, evp->ae_name,
		    sizeof(evn_map.en_name)) < sizeof(evn_map.en_name)) {
			evn_map.en_number = evp->ae_number;
			(void)audit_set_event(&evn_map, sizeof(evn_map));
		}

		/*
		 * Set the event-to-class mapping entry.
		 */
		evc_map.ec_number = evp->ae_number;
		evc_map.ec_class = evp->ae_class;
		if (audit_set_class(&evc_map, sizeof(evc_map)) == 0)
			ctr++;
	}
	endauevent();
	free(ev.ae_name);
	free(ev.ae_desc);

	return (ctr);
}

/*
 * Get the non-attributable event string and set the kernel mask.  Return:
 *	ADE_NOERR	on success,
 *	ADE_PARSE	error parsing audit_control(5),
 *	ADE_AUDITON	error setting the mask using auditon(2).
 */
int
auditd_set_namask(void)
{
	au_mask_t aumask;
	char naeventstr[NA_EVENT_STR_SIZE];

	if (getacna(naeventstr, NA_EVENT_STR_SIZE) != 0 ||
	    getauditflagsbin(naeventstr, &aumask) != 0)
		return (ADE_PARSE);

	if (audit_set_kmask(&aumask, sizeof(aumask)) != 0)
		return (ADE_AUDITON);

	return (ADE_NOERR);
}

/*
 * Set the audit control policy if a policy is configured in audit_control(5),
 * implement the policy. However, if one isn't defined or if there is an error
 * parsing the control file, set AUDIT_CNT to avoid leaving the system in a
 * fragile state.  Return:
 *	ADE_NOERR	on success,
 *	ADE_PARSE	error parsing audit_control(5),
 *	ADE_AUDITON	error setting policy using auditon(2).
 */
int
auditd_set_policy(void)
{
	int policy;
	char polstr[POL_STR_SIZE];

	if (getacpol(polstr, POL_STR_SIZE) != 0 ||
	    au_strtopol(polstr, &policy) != 0) {
		policy = AUDIT_CNT;
		if (audit_set_policy(&policy) != 0)
			return (ADE_AUDITON);
		return (ADE_PARSE);
	}

	if (audit_set_policy(&policy) != 0)
		return (ADE_AUDITON);

	return (ADE_NOERR);
}

/*
 * Set trail rotation size.  Return:
 *	ADE_NOERR	on success,
 *	ADE_PARSE	error parsing audit_control(5),
 *	ADE_AUDITON	error setting file size using auditon(2).
 */
int
auditd_set_fsize(void)
{
	size_t filesz;
	au_fstat_t au_fstat;

	/*
	 * Set trail rotation size.
	 */
	if (getacfilesz(&filesz) != 0)
		return (ADE_PARSE);

	bzero(&au_fstat, sizeof(au_fstat));
	au_fstat.af_filesz = filesz;
	if (audit_set_fsize(&au_fstat, sizeof(au_fstat)) != 0)
		return (ADE_AUDITON);

	return (ADE_NOERR);
}

/*
 * Set trail rotation size.  Return:
 *	ADE_NOERR	on success,
 *	ADE_PARSE	error parsing audit_control(5),
 *	ADE_AUDITON	error setting queue size using auditon(2).
 */
int
auditd_set_qsize(void)
{
	int qsz;
	au_qctrl_t au_qctrl;

	/*
	 * Set trail rotation size.
	 */
	if (getacqsize(&qsz) != 0)
		return (ADE_PARSE);

	if (audit_get_qctrl(&au_qctrl, sizeof(au_qctrl)) != 0)
		return (ADE_AUDITON);
	if (qsz != USE_DEFAULT_QSZ)
		au_qctrl.aq_hiwater = qsz;
	if (audit_set_qctrl(&au_qctrl, sizeof(au_qctrl)) != 0)
		return (ADE_AUDITON);

	return (ADE_NOERR);
}

static void
inject_dist(const char *fromname, char *toname, size_t tonamesize)
{
	char *ptr;

	ptr = strrchr(fromname, '/');
	assert(ptr != NULL);
	assert(ptr - fromname < (ssize_t)tonamesize);
	strlcpy(toname, fromname, ptr - fromname + 1);
	strlcat(toname, "/dist/", tonamesize);
	strlcat(toname, ptr + 1, tonamesize);
}

static int
auditdist_link(const char *filename)
{
	char fname[MAXPATHLEN];

	if (auditd_dist) {
		inject_dist(filename, fname, sizeof(fname));
		/* Ignore errors. */
		(void) link(filename, fname);
	}

	return (0);
}

int
auditd_rename(const char *fromname, const char *toname)
{
	char fname[MAXPATHLEN], tname[MAXPATHLEN];

	if (auditd_dist) {
		inject_dist(fromname, fname, sizeof(fname));
		inject_dist(toname, tname, sizeof(tname));
		/* Ignore errors. */
		(void) rename(fname, tname);
	}

	return (rename(fromname, toname));
}

/*
 * Create the new audit file with appropriate permissions and ownership.
 * Call auditctl(2) for this file.
 * Try to clean up if something goes wrong.
 * *errorp is modified only on auditctl(2) failure.
 */
static int
open_trail(char *fname, gid_t gid, int *errorp)
{
	int fd;

	/* XXXPJD: What should we do if the file already exists? */
	fd = open(fname, O_RDONLY | O_CREAT, S_IRUSR);
	if (fd < 0)
		return (-1);
	if (fchown(fd, -1, gid) < 0 || fchmod(fd, S_IRUSR | S_IRGRP) < 0) {
		(void) close(fd);
		(void) unlink(fname);
		return (-1);
	}
	(void) close(fd);
	if (auditctl(fname) < 0) {
		*errorp = errno;
		(void) unlink(fname);
		return (-1);
	}
	(void) auditdist_link(fname);
	return (0);
}

/*
 * Create the new audit trail file, swap with existing audit file.  Arguments
 * include timestamp for the filename, a pointer to a string for returning the
 * new file name, GID for trail file, and audit_warn function pointer for
 * 'getacdir()' errors.  Returns:
 *	ADE_NOERR	on success,
 *	ADE_STRERR	if the file name string could not be created,
 *	ADE_SWAPERR	if the audit trail file could not be swapped,
 *	ADE_ACTL	if the auditctl(2) call failed but file swap still
 *			successful.
 *	ADE_ACTLERR	if the auditctl(2) call failed and file swap failed.
 *	ADE_SYMLINK	if symlink(2) failed updating the current link.
 */
int
auditd_swap_trail(char *TS, char **newfile, gid_t gid,
    int (*warn_getacdir)(char *))
{
	char timestr[FILENAME_LEN + 1];
	char *fn;
	struct dir_ent *dirent;
	int saverrno = 0;

	if (strlen(TS) != TIMESTAMP_LEN ||
	    snprintf(timestr, sizeof(timestr), "%s.%s", TS,
	    NOT_TERMINATED) < 0) {
		errno = EINVAL;
		return (ADE_STRERR);
	}

	/* Try until we succeed. */
	TAILQ_FOREACH(dirent, &dir_q, dirs) {
		if (dirent->hardlim)
			continue;
		if ((fn = affixdir(timestr, dirent)) == NULL)
			return (ADE_STRERR);

		/*
		 * Create the file and pass to the kernel if all went well.
		 */
		if (open_trail(fn, gid, &saverrno) == 0) {
			/* Success. */
			*newfile = fn;
			if (saverrno) {
				/*
				 * auditctl() failed but still
				 * successful. Return errno and "soft"
				 * error.
				 */
				errno = saverrno;
				return (ADE_ACTL);
			}
			return (ADE_NOERR);
		}
		/*
		 * auditctl failed setting log file. Try again.
		 */
		/*
		 * Tell the administrator about lack of permissions for dir.
		 */
		if (warn_getacdir != NULL)
			(*warn_getacdir)(dirent->dirname);
	}
	if (saverrno) {
		errno = saverrno;
		return (ADE_ACTLERR);
	} else
		return (ADE_SWAPERR);
}

/*
 * Mask calling process from being audited. Returns:
 *	ADE_NOERR	on success,
 *	ADE_SETAUDIT	if setaudit(2) fails.
 */
#ifdef __APPLE__
int
auditd_prevent_audit(void)
{
	auditinfo_addr_t aia;

	/*
	 * To prevent event feedback cycles and avoid audit becoming stalled if
	 * auditing is suspended we mask this processes events from being
	 * audited.  We allow the uid, tid, and mask fields to be implicitly
	 * set to zero, but do set the audit session ID to the PID.
	 *
	 * XXXRW: Is there more to it than this?
	 */
	bzero(&aia, sizeof(aia));
	aia.ai_asid = AU_ASSIGN_ASID;
	aia.ai_termid.at_type = AU_IPv4;
	if (setaudit_addr(&aia, sizeof(aia)) != 0)
		return (ADE_SETAUDIT);
	return (ADE_NOERR);
}
#else
int
auditd_prevent_audit(void)
{
	auditinfo_t ai;

	/*
	 * To prevent event feedback cycles and avoid audit becoming stalled if
	 * auditing is suspended we mask this processes events from being
	 * audited.  We allow the uid, tid, and mask fields to be implicitly
	 * set to zero, but do set the audit session ID to the PID.
	 *
	 * XXXRW: Is there more to it than this?
	 */
	bzero(&ai, sizeof(ai));
	ai.ai_asid = getpid();
	if (setaudit(&ai) != 0)
		return (ADE_SETAUDIT);
	return (ADE_NOERR);
}
#endif /* !__APPLE__ */

/*
 * Generate and submit audit record for audit startup or shutdown.  The event
 * argument can be AUE_audit_recovery, AUE_audit_startup or
 * AUE_audit_shutdown. The path argument will add a path token, if not NULL.
 * Returns:
 *	AUE_NOERR	on success,
 *	ADE_NOMEM	if memory allocation fails,
 *	ADE_AU_OPEN	if au_open(3) fails,
 *	ADE_AU_CLOSE	if au_close(3) fails.
 */
int
auditd_gen_record(int event, char *path)
{
	int aufd;
	uid_t uid;
	pid_t pid;
	char *autext = NULL;
	token_t *tok;
	struct auditinfo_addr aia;

	if (event == AUE_audit_startup)
		asprintf(&autext, "%s::Audit startup", getprogname());
	else if (event == AUE_audit_shutdown)
		asprintf(&autext, "%s::Audit shutdown", getprogname());
	else if (event == AUE_audit_recovery)
		asprintf(&autext, "%s::Audit recovery", getprogname());
	else
		return (ADE_INVAL);
	if (autext == NULL)
		return (ADE_NOMEM);

	if ((aufd = au_open()) == -1) {
		free(autext);
		return (ADE_AU_OPEN);
	}
	bzero(&aia, sizeof(aia));
	uid = getuid(); pid = getpid();
	if ((tok = au_to_subject32_ex(uid, geteuid(), getegid(), uid, getgid(),
	    pid, pid, &aia.ai_termid)) != NULL)
		au_write(aufd, tok);
	if ((tok = au_to_text(autext)) != NULL)
		au_write(aufd, tok);
	free(autext);
	if (path != NULL && (tok = au_to_path(path)) != NULL)
		au_write(aufd, tok);
	if ((tok = au_to_return32(0, 0)) != NULL)
		au_write(aufd, tok);
	if (au_close(aufd, 1, event) == -1)
		return (ADE_AU_CLOSE);

	return (ADE_NOERR);
}

/*
 * Check for a 'current' symlink and do crash recovery, if needed. Create a new
 * 'current' symlink. The argument 'curfile' is the file the 'current' symlink
 * should point to.  Returns:
 *	ADE_NOERR	on success,
 *	ADE_AU_OPEN	if au_open(3) fails,
 *	ADE_AU_CLOSE	if au_close(3) fails.
 *	ADE_RENAME	if error renaming audit trail file,
 *	ADE_READLINK	if error reading the 'current' link,
 *	ADE_SYMLINK	if error creating 'current' link.
 */
int
auditd_new_curlink(char *curfile)
{
	int len, err;
	char *ptr;
	char *path = NULL;
	struct stat sb;
	char recoveredname[MAXPATHLEN];
	char newname[MAXPATHLEN];

	/*
	 * Check to see if audit was shutdown properly.  If not, clean up,
	 * recover previous audit trail file, and generate audit record.
	 */
	len = readlink(AUDIT_CURRENT_LINK, recoveredname,
	    sizeof(recoveredname) - 1);
	if (len > 0) {
		/* 'current' exist but is it pointing at a valid file?  */
		recoveredname[len++] = '\0';
		if (stat(recoveredname, &sb) == 0) {
			/* Yes, rename it to a crash recovery file. */
			strlcpy(newname, recoveredname, sizeof(newname));

			if ((ptr = strstr(newname, NOT_TERMINATED)) != NULL) {
				memcpy(ptr, CRASH_RECOVERY, POSTFIX_LEN);
				if (auditd_rename(recoveredname, newname) != 0)
					return (ADE_RENAME);
			} else
				return (ADE_STRERR);

			path = newname;
		}

		/* 'current' symlink is (now) invalid so remove it. */
		(void) unlink(AUDIT_CURRENT_LINK);

		/* Note the crash recovery in current audit trail */
		err = auditd_gen_record(AUE_audit_recovery, path);
		if (err)
			return (err);
	}

	if (len < 0 && errno != ENOENT)
		return (ADE_READLINK);

	if (symlink(curfile, AUDIT_CURRENT_LINK) != 0)
		return (ADE_SYMLINK);

	return (0);
}

/*
 * Do just what we need to quickly start auditing.  Assume no system logging or
 * notify.  Return:
 *   0	 on success,
 *  -1   on failure.
 */
int
audit_quick_start(void)
{
	int err;
	char *newfile = NULL;
	time_t tt;
	char TS[TIMESTAMP_LEN + 1];
	int ret = 0;

	/*
	 * Mask auditing of this process.
	 */
	if (auditd_prevent_audit() != 0)
		return (-1);

	/*
	 * Read audit_control and get log directories.
	 */
	err = auditd_read_dirs(NULL, NULL);
	if (err != ADE_NOERR && err != ADE_SOFTLIM)
		return (-1);

	/*
	 * Setup trail file distribution.
	 */
	(void) auditd_set_dist();

	/*
	 *  Create a new audit trail log.
	 */
	if (getTSstr(tt, TS, sizeof(TS)) != 0)
		return (-1);
	err = auditd_swap_trail(TS, &newfile, getgid(), NULL);
	if (err != ADE_NOERR && err != ADE_ACTL) {
		ret = -1;
		goto out;
	}

	/*
	 * Add the current symlink and recover from crash, if needed.
	 */
	if (auditd_new_curlink(newfile) != 0) {
		ret = -1;
		goto out;
	}

	/*
	 * At this point auditing has started so generate audit start-up record.
	 */
	if (auditd_gen_record(AUE_audit_startup, NULL) != 0) {
		ret = -1;
		goto out;
	}

	/*
	 *  Configure the audit controls.
	 */
	(void) auditd_set_evcmap();
	(void) auditd_set_namask();
	(void) auditd_set_policy();
	(void) auditd_set_fsize();
	(void) auditd_set_minfree();
	(void) auditd_set_host();

out:
	if (newfile != NULL)
		free(newfile);

	return (ret);
}

/*
 * Shut down auditing quickly.  Assumes that is only called on system shutdown.
 * Returns:
 *	 0	on success,
 *	-1	on failure.
 */
int
audit_quick_stop(void)
{
	int len;
	int cond;
	char *ptr;
	time_t tt;
	char oldname[MAXPATHLEN];
	char newname[MAXPATHLEN];
	char TS[TIMESTAMP_LEN + 1];

	/*
	 * Auditing already disabled?
	 */
	if (audit_get_cond(&cond) != 0)
		return (-1);
	if (cond == AUC_NOAUDIT)
		return (0);

	/*
	 *  Generate audit shutdown record.
	 */
	(void) auditd_gen_record(AUE_audit_shutdown, NULL);

	/*
	 * Shutdown auditing in the kernel.
	 */
	cond = AUC_DISABLED;
	if (audit_set_cond(&cond) != 0)
		return (-1);
#ifdef	__BSM_INTERNAL_NOTIFY_KEY
	notify_post(__BSM_INTERNAL_NOTIFY_KEY);
#endif

	/*
	 * Rename last audit trail and remove 'current' link.
	 */
	len = readlink(AUDIT_CURRENT_LINK, oldname, sizeof(oldname) - 1);
	if (len < 0)
		return (-1);
	oldname[len++] = '\0';

	if (getTSstr(tt, TS, sizeof(TS)) != 0)
		return (-1);

	strlcpy(newname, oldname, sizeof(newname));

	if ((ptr = strstr(newname, NOT_TERMINATED)) != NULL) {
		memcpy(ptr, TS, POSTFIX_LEN);
		if (auditd_rename(oldname, newname) != 0)
			return (-1);
	} else
		return (-1);

	(void) unlink(AUDIT_CURRENT_LINK);

	return (0);
}
