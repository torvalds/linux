/*
** This file is in the public domain, so clarified as of
** 2009-05-17 by Arthur David Olson.
*/

#ifndef lint
static const char rcsid[] =
  "$FreeBSD$";
static char	elsieid[] = "@(#)zdump.c	8.10";
#endif /* not lint */

/*
** This code has been made independent of the rest of the time
** conversion package to increase confidence in the verification it provides.
** You can use this code to help in verifying other implementations.
*/

#include <err.h>
#include <stdio.h>	/* for stdout, stderr */
#include <stdlib.h>	/* for exit, malloc, atoi */
#include <string.h>	/* for strcpy */
#include <sys/types.h>	/* for time_t */
#include <time.h>	/* for struct tm */
#include <unistd.h>
#include <float.h>	/* for FLT_MAX and DBL_MAX */
#include <ctype.h>	/* for isalpha et al. */
#ifndef isascii
#define isascii(x) 1
#endif /* !defined isascii */

#ifndef ZDUMP_LO_YEAR
#define ZDUMP_LO_YEAR	(-500)
#endif /* !defined ZDUMP_LO_YEAR */

#ifndef ZDUMP_HI_YEAR
#define ZDUMP_HI_YEAR	2500
#endif /* !defined ZDUMP_HI_YEAR */

#ifndef MAX_STRING_LENGTH
#define MAX_STRING_LENGTH	1024
#endif /* !defined MAX_STRING_LENGTH */

#ifndef TRUE
#define TRUE		1
#endif /* !defined TRUE */

#ifndef FALSE
#define FALSE		0
#endif /* !defined FALSE */

#ifndef EXIT_SUCCESS
#define EXIT_SUCCESS	0
#endif /* !defined EXIT_SUCCESS */

#ifndef EXIT_FAILURE
#define EXIT_FAILURE	1
#endif /* !defined EXIT_FAILURE */

#ifndef SECSPERMIN
#define SECSPERMIN	60
#endif /* !defined SECSPERMIN */

#ifndef MINSPERHOUR
#define MINSPERHOUR	60
#endif /* !defined MINSPERHOUR */

#ifndef SECSPERHOUR
#define SECSPERHOUR	(SECSPERMIN * MINSPERHOUR)
#endif /* !defined SECSPERHOUR */

#ifndef HOURSPERDAY
#define HOURSPERDAY	24
#endif /* !defined HOURSPERDAY */

#ifndef EPOCH_YEAR
#define EPOCH_YEAR	1970
#endif /* !defined EPOCH_YEAR */

#ifndef TM_YEAR_BASE
#define TM_YEAR_BASE	1900
#endif /* !defined TM_YEAR_BASE */

#ifndef DAYSPERNYEAR
#define DAYSPERNYEAR	365
#endif /* !defined DAYSPERNYEAR */

#ifndef isleap
#define isleap(y) (((y) % 4) == 0 && (((y) % 100) != 0 || ((y) % 400) == 0))
#endif /* !defined isleap */

#ifndef isleap_sum
/*
** See tzfile.h for details on isleap_sum.
*/
#define isleap_sum(a, b)	isleap((a) % 400 + (b) % 400)
#endif /* !defined isleap_sum */

#define SECSPERDAY	((long) SECSPERHOUR * HOURSPERDAY)
#define SECSPERNYEAR	(SECSPERDAY * DAYSPERNYEAR)
#define SECSPERLYEAR	(SECSPERNYEAR + SECSPERDAY)

#ifndef HAVE_GETTEXT
#define HAVE_GETTEXT 0
#endif
#if HAVE_GETTEXT
#include "locale.h"	/* for setlocale */
#include "libintl.h"
#endif /* HAVE_GETTEXT */

#ifndef GNUC_or_lint
#ifdef lint
#define GNUC_or_lint
#else /* !defined lint */
#ifdef __GNUC__
#define GNUC_or_lint
#endif /* defined __GNUC__ */
#endif /* !defined lint */
#endif /* !defined GNUC_or_lint */

#ifndef INITIALIZE
#ifdef GNUC_or_lint
#define INITIALIZE(x)	((x) = 0)
#else /* !defined GNUC_or_lint */
#define INITIALIZE(x)
#endif /* !defined GNUC_or_lint */
#endif /* !defined INITIALIZE */

/*
** For the benefit of GNU folk...
** `_(MSGID)' uses the current locale's message library string for MSGID.
** The default is to use gettext if available, and use MSGID otherwise.
*/

#ifndef _
#if HAVE_GETTEXT
#define _(msgid) gettext(msgid)
#else /* !(HAVE_GETTEXT) */
#define _(msgid) msgid
#endif /* !(HAVE_GETTEXT) */
#endif /* !defined _ */

#ifndef TZ_DOMAIN
#define TZ_DOMAIN "tz"
#endif /* !defined TZ_DOMAIN */

extern char **	environ;
extern char *	tzname[2];

static time_t	absolute_min_time;
static time_t	absolute_max_time;
static size_t	longest;
static char *	progname;
static int	warned;

static void	usage(FILE *stream, int status);
static char *	abbr(struct tm * tmp);
static void	abbrok(const char * abbrp, const char * zone);
static long	delta(struct tm * newp, struct tm * oldp);
static void	dumptime(const struct tm * tmp);
static time_t	hunt(char * name, time_t lot, time_t	hit);
static void	setabsolutes(void);
static void	show(char * zone, time_t t, int v);
static const char *	tformat(void);
static time_t	yeartot(long y);

#ifndef TYPECHECK
#define my_localtime	localtime
#else /* !defined TYPECHECK */
static struct tm *
my_localtime(tp)
time_t *	tp;
{
	register struct tm *	tmp;

	tmp = localtime(tp);
	if (tp != NULL && tmp != NULL) {
		struct tm	tm;
		register time_t	t;

		tm = *tmp;
		t = mktime(&tm);
		if (t - *tp >= 1 || *tp - t >= 1) {
			(void) fflush(stdout);
			(void) fprintf(stderr, "\n%s: ", progname);
			(void) fprintf(stderr, tformat(), *tp);
			(void) fprintf(stderr, " ->");
			(void) fprintf(stderr, " year=%d", tmp->tm_year);
			(void) fprintf(stderr, " mon=%d", tmp->tm_mon);
			(void) fprintf(stderr, " mday=%d", tmp->tm_mday);
			(void) fprintf(stderr, " hour=%d", tmp->tm_hour);
			(void) fprintf(stderr, " min=%d", tmp->tm_min);
			(void) fprintf(stderr, " sec=%d", tmp->tm_sec);
			(void) fprintf(stderr, " isdst=%d", tmp->tm_isdst);
			(void) fprintf(stderr, " -> ");
			(void) fprintf(stderr, tformat(), t);
			(void) fprintf(stderr, "\n");
		}
	}
	return tmp;
}
#endif /* !defined TYPECHECK */

static void
abbrok(abbrp, zone)
const char * const	abbrp;
const char * const	zone;
{
	register const char *	cp;
	register char *		wp;

	if (warned)
		return;
	cp = abbrp;
	wp = NULL;
	while (isascii((unsigned char) *cp) &&
		(isalnum((unsigned char)*cp) || *cp == '-' || *cp == '+'))
		++cp;
	if (cp - abbrp < 3)
		wp = _("has fewer than 3 characters");
	else if (cp - abbrp > 6)
		wp = _("has more than 6 characters");
	else if (*cp)
		wp = "has characters other than ASCII alphanumerics, '-' or '+'";
	else
		return;
	(void) fflush(stdout);
	(void) fprintf(stderr,
		_("%s: warning: zone \"%s\" abbreviation \"%s\" %s\n"),
		progname, zone, abbrp, wp);
	warned = TRUE;
}

int
main(argc, argv)
int	argc;
char *	argv[];
{
	register int		i;
	register int		c;
	register int		vflag;
	register char *		cutarg;
	register long		cutloyear = ZDUMP_LO_YEAR;
	register long		cuthiyear = ZDUMP_HI_YEAR;
	register time_t		cutlotime;
	register time_t		cuthitime;
	register char **	fakeenv;
	time_t			now;
	time_t			t;
	time_t			newt;
	struct tm		tm;
	struct tm		newtm;
	register struct tm *	tmp;
	register struct tm *	newtmp;

	progname=argv[0];
	INITIALIZE(cutlotime);
	INITIALIZE(cuthitime);
#if HAVE_GETTEXT
	(void) setlocale(LC_MESSAGES, "");
#ifdef TZ_DOMAINDIR
	(void) bindtextdomain(TZ_DOMAIN, TZ_DOMAINDIR);
#endif /* TEXTDOMAINDIR */
	(void) textdomain(TZ_DOMAIN);
#endif /* HAVE_GETTEXT */
	for (i = 1; i < argc; ++i)
		if (strcmp(argv[i], "--version") == 0) {
			errx(EXIT_SUCCESS, "%s", elsieid);
		} else if (strcmp(argv[i], "--help") == 0) {
			usage(stdout, EXIT_SUCCESS);
		}
	vflag = 0;
	cutarg = NULL;
	while ((c = getopt(argc, argv, "c:v")) == 'c' || c == 'v')
		if (c == 'v')
			vflag = 1;
		else	cutarg = optarg;
	if ((c != -1) ||
		(optind == argc - 1 && strcmp(argv[optind], "=") == 0)) {
			usage(stderr, EXIT_FAILURE);
	}
	if (vflag) {
		if (cutarg != NULL) {
			long	lo;
			long	hi;
			char	dummy;

			if (sscanf(cutarg, "%ld%c", &hi, &dummy) == 1) {
				cuthiyear = hi;
			} else if (sscanf(cutarg, "%ld,%ld%c",
				&lo, &hi, &dummy) == 2) {
					cutloyear = lo;
					cuthiyear = hi;
			} else {
(void) fprintf(stderr, _("%s: wild -c argument %s\n"),
					progname, cutarg);
				exit(EXIT_FAILURE);
			}
		}
		setabsolutes();
		cutlotime = yeartot(cutloyear);
		cuthitime = yeartot(cuthiyear);
	}
	(void) time(&now);
	longest = 0;
	for (i = optind; i < argc; ++i)
		if (strlen(argv[i]) > longest)
			longest = strlen(argv[i]);
	{
		register int	from;
		register int	to;

		for (i = 0;  environ[i] != NULL; ++i)
			continue;
		fakeenv = (char **) malloc((size_t) ((i + 2) *
			sizeof *fakeenv));
		if (fakeenv == NULL ||
			(fakeenv[0] = (char *) malloc((size_t) (longest +
				4))) == NULL)
					errx(EXIT_FAILURE,
					     _("malloc() failed"));
		to = 0;
		(void) strcpy(fakeenv[to++], "TZ=");
		for (from = 0; environ[from] != NULL; ++from)
			if (strncmp(environ[from], "TZ=", 3) != 0)
				fakeenv[to++] = environ[from];
		fakeenv[to] = NULL;
		environ = fakeenv;
	}
	for (i = optind; i < argc; ++i) {
		static char	buf[MAX_STRING_LENGTH];

		(void) strcpy(&fakeenv[0][3], argv[i]);
		if (!vflag) {
			show(argv[i], now, FALSE);
			continue;
		}
		warned = FALSE;
		t = absolute_min_time;
		show(argv[i], t, TRUE);
		t += SECSPERHOUR * HOURSPERDAY;
		show(argv[i], t, TRUE);
		if (t < cutlotime)
			t = cutlotime;
		tmp = my_localtime(&t);
		if (tmp != NULL) {
			tm = *tmp;
			(void) strncpy(buf, abbr(&tm), (sizeof buf) - 1);
		}
		for ( ; ; ) {
			if (t >= cuthitime || t >= cuthitime - SECSPERHOUR * 12)
				break;
			newt = t + SECSPERHOUR * 12;
			newtmp = localtime(&newt);
			if (newtmp != NULL)
				newtm = *newtmp;
			if ((tmp == NULL || newtmp == NULL) ? (tmp != newtmp) :
				(delta(&newtm, &tm) != (newt - t) ||
				newtm.tm_isdst != tm.tm_isdst ||
				strcmp(abbr(&newtm), buf) != 0)) {
					newt = hunt(argv[i], t, newt);
					newtmp = localtime(&newt);
					if (newtmp != NULL) {
						newtm = *newtmp;
						(void) strncpy(buf,
							abbr(&newtm),
							(sizeof buf) - 1);
					}
			}
			t = newt;
			tm = newtm;
			tmp = newtmp;
		}
		t = absolute_max_time;
		t -= SECSPERHOUR * HOURSPERDAY;
		show(argv[i], t, TRUE);
		t += SECSPERHOUR * HOURSPERDAY;
		show(argv[i], t, TRUE);
	}
	if (fflush(stdout) || ferror(stdout))
		errx(EXIT_FAILURE, _("error writing standard output"));
	exit(EXIT_SUCCESS);
	/* If exit fails to exit... */
	return(EXIT_FAILURE);
}

static void
setabsolutes(void)
{
	if (0.5 == (time_t) 0.5) {
		/*
		** time_t is floating.
		*/
		if (sizeof (time_t) == sizeof (float)) {
			absolute_min_time = (time_t) -FLT_MAX;
			absolute_max_time = (time_t) FLT_MAX;
		} else if (sizeof (time_t) == sizeof (double)) {
			absolute_min_time = (time_t) -DBL_MAX;
			absolute_max_time = (time_t) DBL_MAX;
		} else {
			(void) fprintf(stderr,
_("%s: use of -v on system with floating time_t other than float or double\n"),
				progname);
			exit(EXIT_FAILURE);
		}
	} else if (0 > (time_t) -1) {
		/*
		** time_t is signed.  Assume overflow wraps around.
		*/
		time_t t = 0;
		time_t t1 = 1;

		while (t < t1) {
			t = t1;
			t1 = 2 * t1 + 1;
		}

		absolute_max_time = t;
		t = -t;
		absolute_min_time = t - 1;
		if (t < absolute_min_time)
			absolute_min_time = t;
	} else {
		/*
		** time_t is unsigned.
		*/
		absolute_min_time = 0;
		absolute_max_time = absolute_min_time - 1;
	}
}

static time_t
yeartot(y)
const long	y;
{
	register long	myy;
	register long	seconds;
	register time_t	t;

	myy = EPOCH_YEAR;
	t = 0;
	while (myy != y) {
		if (myy < y) {
			seconds = isleap(myy) ? SECSPERLYEAR : SECSPERNYEAR;
			++myy;
			if (t > absolute_max_time - seconds) {
				t = absolute_max_time;
				break;
			}
			t += seconds;
		} else {
			--myy;
			seconds = isleap(myy) ? SECSPERLYEAR : SECSPERNYEAR;
			if (t < absolute_min_time + seconds) {
				t = absolute_min_time;
				break;
			}
			t -= seconds;
		}
	}
	return t;
}

static void
usage(FILE *stream, int status)
{
	fprintf(stream,
_("usage: %s [--version] [-v] [--help] [-c [loyear,]hiyear] zonename ...\n\
\n\
Report bugs to tz@elsie.nci.nih.gov.\n"), progname);
	exit(status);
}

static time_t
hunt(char *name, time_t lot, time_t hit)
{
	time_t			t;
	long			diff;
	struct tm		lotm;
	register struct tm *	lotmp;
	struct tm		tm;
	register struct tm *	tmp;
	char			loab[MAX_STRING_LENGTH];

	lotmp = my_localtime(&lot);
	if (lotmp != NULL) {
		lotm = *lotmp;
		(void) strncpy(loab, abbr(&lotm), (sizeof loab) - 1);
	}
	for ( ; ; ) {
		diff = (long) (hit - lot);
		if (diff < 2)
			break;
		t = lot;
		t += diff / 2;
		if (t <= lot)
			++t;
		else if (t >= hit)
			--t;
		tmp = my_localtime(&t);
		if (tmp != NULL)
			tm = *tmp;
		if ((lotmp == NULL || tmp == NULL) ? (lotmp == tmp) :
			(delta(&tm, &lotm) == (t - lot) &&
			tm.tm_isdst == lotm.tm_isdst &&
			strcmp(abbr(&tm), loab) == 0)) {
				lot = t;
				lotm = tm;
				lotmp = tmp;
		} else	hit = t;
	}
	show(name, lot, TRUE);
	show(name, hit, TRUE);
	return hit;
}

/*
** Thanks to Paul Eggert for logic used in delta.
*/

static long
delta(newp, oldp)
struct tm *	newp;
struct tm *	oldp;
{
	register long	result;
	register int	tmy;

	if (newp->tm_year < oldp->tm_year)
		return -delta(oldp, newp);
	result = 0;
	for (tmy = oldp->tm_year; tmy < newp->tm_year; ++tmy)
		result += DAYSPERNYEAR + isleap_sum(tmy, TM_YEAR_BASE);
	result += newp->tm_yday - oldp->tm_yday;
	result *= HOURSPERDAY;
	result += newp->tm_hour - oldp->tm_hour;
	result *= MINSPERHOUR;
	result += newp->tm_min - oldp->tm_min;
	result *= SECSPERMIN;
	result += newp->tm_sec - oldp->tm_sec;
	return result;
}

static void
show(char *zone, time_t t, int v)
{
	register struct tm *	tmp;

	(void) printf("%-*s  ", (int) longest, zone);
	if (v) {
		tmp = gmtime(&t);
		if (tmp == NULL) {
			(void) printf(tformat(), t);
		} else {
			dumptime(tmp);
			(void) printf(" UTC");
		}
		(void) printf(" = ");
	}
	tmp = my_localtime(&t);
	dumptime(tmp);
	if (tmp != NULL) {
		if (*abbr(tmp) != '\0')
			(void) printf(" %s", abbr(tmp));
		if (v) {
			(void) printf(" isdst=%d", tmp->tm_isdst);
#ifdef TM_GMTOFF
			(void) printf(" gmtoff=%ld", tmp->TM_GMTOFF);
#endif /* defined TM_GMTOFF */
		}
	}
	(void) printf("\n");
	if (tmp != NULL && *abbr(tmp) != '\0')
		abbrok(abbr(tmp), zone);
}

static char *
abbr(tmp)
struct tm *	tmp;
{
	register char *	result;
	static char	nada;

	if (tmp->tm_isdst != 0 && tmp->tm_isdst != 1)
		return &nada;
	result = tzname[tmp->tm_isdst];
	return (result == NULL) ? &nada : result;
}

/*
** The code below can fail on certain theoretical systems;
** it works on all known real-world systems as of 2004-12-30.
*/

static const char *
tformat(void)
{
	if (0.5 == (time_t) 0.5) {	/* floating */
		if (sizeof (time_t) > sizeof (double))
			return "%Lg";
		return "%g";
	}
	if (0 > (time_t) -1) {		/* signed */
		if (sizeof (time_t) > sizeof (long))
			return "%lld";
		if (sizeof (time_t) > sizeof (int))
			return "%ld";
		return "%d";
	}
	if (sizeof (time_t) > sizeof (unsigned long))
		return "%llu";
	if (sizeof (time_t) > sizeof (unsigned int))
		return "%lu";
	return "%u";
}

static void
dumptime(timeptr)
register const struct tm *	timeptr;
{
	static const char	wday_name[][3] = {
		"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
	};
	static const char	mon_name[][3] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
	};
	register const char *	wn;
	register const char *	mn;
	register int		lead;
	register int		trail;

	if (timeptr == NULL) {
		(void) printf("NULL");
		return;
	}
	/*
	** The packaged versions of localtime and gmtime never put out-of-range
	** values in tm_wday or tm_mon, but since this code might be compiled
	** with other (perhaps experimental) versions, paranoia is in order.
	*/
	if (timeptr->tm_wday < 0 || timeptr->tm_wday >=
		(int) (sizeof wday_name / sizeof wday_name[0]))
			wn = "???";
	else		wn = wday_name[timeptr->tm_wday];
	if (timeptr->tm_mon < 0 || timeptr->tm_mon >=
		(int) (sizeof mon_name / sizeof mon_name[0]))
			mn = "???";
	else		mn = mon_name[timeptr->tm_mon];
	(void) printf("%.3s %.3s%3d %.2d:%.2d:%.2d ",
		wn, mn,
		timeptr->tm_mday, timeptr->tm_hour,
		timeptr->tm_min, timeptr->tm_sec);
#define DIVISOR	10
	trail = timeptr->tm_year % DIVISOR + TM_YEAR_BASE % DIVISOR;
	lead = timeptr->tm_year / DIVISOR + TM_YEAR_BASE / DIVISOR +
		trail / DIVISOR;
	trail %= DIVISOR;
	if (trail < 0 && lead > 0) {
		trail += DIVISOR;
		--lead;
	} else if (lead < 0 && trail > 0) {
		trail -= DIVISOR;
		++lead;
	}
	if (lead == 0)
		(void) printf("%d", trail);
	else	(void) printf("%d%d", lead, ((trail < 0) ? -trail : trail));
}
