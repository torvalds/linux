/*	$OpenBSD: mail.c,v 1.27 2019/01/14 08:48:16 schwarze Exp $	*/

/*
 * Mailbox checking code by Robert J. Gibson, adapted for PD ksh by
 * John R. MacMillan
 */

#include <sys/stat.h>
#include <sys/time.h>

#include <string.h>
#include <time.h>

#include "config.h"
#include "sh.h"

#define MBMESSAGE	"you have mail in $_"

typedef struct mbox {
	struct mbox    *mb_next;	/* next mbox in list */
	char	       *mb_path;	/* path to mail file */
	char	       *mb_msg;		/* to announce arrival of new mail */
	time_t		mb_mtime;	/* mtime of mail file */
} mbox_t;

/*
 * $MAILPATH is a linked list of mboxes.  $MAIL is a treated as a
 * special case of $MAILPATH, where the list has only one node.  The
 * same list is used for both since they are exclusive.
 */

static mbox_t	*mplist;
static mbox_t	mbox;
static struct	timespec mlastchkd;	/* when mail was last checked */
static time_t	mailcheck_interval;

static void	munset(mbox_t *); /* free mlist and mval */
static mbox_t * mballoc(char *, char *); /* allocate a new mbox */
static void	mprintit(mbox_t *);

void
mcheck(void)
{
	mbox_t		*mbp;
	struct timespec	 elapsed, now;
	struct tbl	*vp;
	struct stat	 stbuf;
	static int	 first = 1;

	if (mplist)
		mbp = mplist;
	else if ((vp = global("MAIL")) && (vp->flag & ISSET))
		mbp = &mbox;
	else
		mbp = NULL;
	if (mbp == NULL)
		return;

	clock_gettime(CLOCK_MONOTONIC, &now);
	if (first) {
		mlastchkd = now;
		first = 0;
	}
	timespecsub(&now, &mlastchkd, &elapsed);
	if (elapsed.tv_sec >= mailcheck_interval) {
		mlastchkd = now;

		while (mbp) {
			if (mbp->mb_path && stat(mbp->mb_path, &stbuf) == 0 &&
			    S_ISREG(stbuf.st_mode)) {
				if (stbuf.st_size &&
				    mbp->mb_mtime != stbuf.st_mtime &&
				    stbuf.st_atime <= stbuf.st_mtime)
					mprintit(mbp);
				mbp->mb_mtime = stbuf.st_mtime;
			} else {
				/*
				 * Some mail readers remove the mail
				 * file if all mail is read.  If file
				 * does not exist, assume this is the
				 * case and set mtime to zero.
				 */
				mbp->mb_mtime = 0;
			}
			mbp = mbp->mb_next;
		}
	}
}

void
mcset(int64_t interval)
{
	mailcheck_interval = interval;
}

void
mbset(char *p)
{
	struct stat	stbuf;

	afree(mbox.mb_msg, APERM);
	afree(mbox.mb_path, APERM);
	/* Save a copy to protect from export (which munges the string) */
	mbox.mb_path = str_save(p, APERM);
	mbox.mb_msg = NULL;
	if (p && stat(p, &stbuf) == 0 && S_ISREG(stbuf.st_mode))
		mbox.mb_mtime = stbuf.st_mtime;
	else
		mbox.mb_mtime = 0;
}

void
mpset(char *mptoparse)
{
	mbox_t	*mbp;
	char	*mpath, *mmsg, *mval;
	char *p;

	munset( mplist );
	mplist = NULL;
	mval = str_save(mptoparse, APERM);
	while (mval) {
		mpath = mval;
		if ((mval = strchr(mval, ':')) != NULL) {
			*mval = '\0';
			mval++;
		}
		/* POSIX/bourne-shell say file%message */
		for (p = mpath; (mmsg = strchr(p, '%')); ) {
			/* a literal percent? (POSIXism) */
			if (mmsg > mpath && mmsg[-1] == '\\') {
				/* use memmove() to avoid overlap problems */
				memmove(mmsg - 1, mmsg, strlen(mmsg) + 1);
				p = mmsg;
				continue;
			}
			break;
		}
		/* at&t ksh says file?message */
		if (!mmsg && !Flag(FPOSIX))
			mmsg = strchr(mpath, '?');
		if (mmsg) {
			*mmsg = '\0';
			mmsg++;
			if (*mmsg == '\0')
				mmsg = NULL;
		}
		if (*mpath == '\0')
			continue;
		mbp = mballoc(mpath, mmsg);
		mbp->mb_next = mplist;
		mplist = mbp;
	}
}

static void
munset(mbox_t *mlist)
{
	mbox_t	*mbp;

	while (mlist != NULL) {
		mbp = mlist;
		mlist = mbp->mb_next;
		if (!mlist)
			afree(mbp->mb_path, APERM);
		afree(mbp, APERM);
	}
}

static mbox_t *
mballoc(char *p, char *m)
{
	struct stat	stbuf;
	mbox_t	*mbp;

	mbp = alloc(sizeof(mbox_t), APERM);
	mbp->mb_next = NULL;
	mbp->mb_path = p;
	mbp->mb_msg = m;
	if (stat(mbp->mb_path, &stbuf) == 0 && S_ISREG(stbuf.st_mode))
		mbp->mb_mtime = stbuf.st_mtime;
	else
		mbp->mb_mtime = 0;
	return(mbp);
}

static void
mprintit(mbox_t *mbp)
{
	struct tbl	*vp;

#if 0
	/*
	 * I doubt this $_ overloading is bad in /bin/sh mode.  Anyhow, we
	 * crash as the code looks now if we do not set vp.  Now, this is
	 * easy to fix too, but I'd like to see what POSIX says before doing
	 * a change like that.
	 */
	if (!Flag(FSH))
#endif
		/* Ignore setstr errors here (arbitrary) */
		setstr((vp = local("_", false)), mbp->mb_path, KSH_RETURN_ERROR);

	shellf("%s\n", substitute(mbp->mb_msg ? mbp->mb_msg : MBMESSAGE, 0));

	unset(vp, 0);
}
