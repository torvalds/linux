/*
 * ntp_leapsec.c - leap second processing for NTPD
 *
 * Written by Juergen Perlinger (perlinger@ntp.org) for the NTP project.
 * The contents of 'html/copyright.html' apply.
 * ----------------------------------------------------------------------
 * This is an attempt to get the leap second handling into a dedicated
 * module to make the somewhat convoluted logic testable.
 */

#include <config.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>

#include "ntp_types.h"
#include "ntp_fp.h"
#include "ntp_stdlib.h"
#include "ntp_calendar.h"
#include "ntp_leapsec.h"
#include "ntp.h"
#include "vint64ops.h"
#include "lib_strbuf.h"

#include "isc/sha1.h"

static const char * const logPrefix = "leapsecond file";

/* ---------------------------------------------------------------------
 * GCC is rather sticky with its 'const' attribute. We have to do it more
 * explicit than with a cast if we want to get rid of a CONST qualifier.
 * Greetings from the PASCAL world, where casting was only possible via
 * untagged unions...
 */
static inline void*
noconst(
	const void* ptr
	)
{
	union {
		const void * cp;
		void *       vp;
	} tmp;
	tmp.cp = ptr;
	return tmp.vp;
}

/* ---------------------------------------------------------------------
 * Our internal data structure
 */
#define MAX_HIST 10	/* history of leap seconds */

struct leap_info {
	vint64   ttime;	/* transition time (after the step, ntp scale) */
	uint32_t stime;	/* schedule limit (a month before transition)  */
	int16_t  taiof;	/* TAI offset on and after the transition      */
	uint8_t  dynls; /* dynamic: inserted on peer/clock request     */
};
typedef struct leap_info leap_info_t;

struct leap_head {
	vint64   update; /* time of information update                 */
	vint64   expire; /* table expiration time                      */
	uint16_t size;	 /* number of infos in table	               */
	int16_t  base_tai;	/* total leaps before first entry      */
	int16_t  this_tai;	/* current TAI offset	               */
	int16_t  next_tai;	/* TAI offset after 'when'             */
	vint64   dtime;	 /* due time (current era end)                 */
	vint64   ttime;	 /* nominal transition time (next era start)   */
	vint64   stime;	 /* schedule time (when we take notice)        */
	vint64   ebase;	 /* base time of this leap era                 */
	uint8_t  dynls;	 /* next leap is dynamic (by peer request)     */
};
typedef struct leap_head leap_head_t;

struct leap_table {
	leap_signature_t lsig;
	leap_head_t	 head;
	leap_info_t  	 info[MAX_HIST];
};

/* Where we store our tables */
static leap_table_t _ltab[2], *_lptr;
static int/*BOOL*/  _electric;

/* Forward decls of local helpers */
static int    add_range(leap_table_t*, const leap_info_t*);
static char * get_line(leapsec_reader, void*, char*, size_t);
static char * skipws(const char*);
static int    parsefail(const char * cp, const char * ep);
static void   reload_limits(leap_table_t*, const vint64*);
static void   fetch_leap_era(leap_era_t*, const leap_table_t*,
			     const vint64*);
static int    betweenu32(uint32_t, uint32_t, uint32_t);
static void   reset_times(leap_table_t*);
static int    leapsec_add(leap_table_t*, const vint64*, int);
static int    leapsec_raw(leap_table_t*, const vint64 *, int, int);
static const char * lstostr(const vint64 * ts);

/* =====================================================================
 * Get & Set the current leap table
 */

/* ------------------------------------------------------------------ */
leap_table_t *
leapsec_get_table(
	int alternate)
{
	leap_table_t *p1, *p2;

	p1 = _lptr;
	if (p1 == &_ltab[0]) {
		p2 = &_ltab[1];
	} else if (p1 == &_ltab[1]) {
		p2 = &_ltab[0];
	} else {
		p1 = &_ltab[0];
		p2 = &_ltab[1];
		reset_times(p1);
		reset_times(p2);
		_lptr = p1;
	}
	if (alternate) {
		memcpy(p2, p1, sizeof(leap_table_t));
		p1 = p2;
	}

	return p1;
}

/* ------------------------------------------------------------------ */
int/*BOOL*/
leapsec_set_table(
	leap_table_t * pt)
{
	if (pt == &_ltab[0] || pt == &_ltab[1])
		_lptr = pt;
	return _lptr == pt;
}

/* ------------------------------------------------------------------ */
int/*BOOL*/
leapsec_electric(
	int/*BOOL*/ on)
{
	int res = _electric;
	if (on < 0)
		return res;

	_electric = (on != 0);
	if (_electric == res)
		return res;

	if (_lptr == &_ltab[0] || _lptr == &_ltab[1])
		reset_times(_lptr);

	return res;
}

/* =====================================================================
 * API functions that operate on tables
 */

/* ---------------------------------------------------------------------
 * Clear all leap second data. Use it for init & cleanup
 */
void
leapsec_clear(
	leap_table_t * pt)
{
	memset(&pt->lsig, 0, sizeof(pt->lsig));
	memset(&pt->head, 0, sizeof(pt->head));
	reset_times(pt);
}

/* ---------------------------------------------------------------------
 * Load a leap second file and check expiration on the go
 */
int/*BOOL*/
leapsec_load(
	leap_table_t * pt  ,
	leapsec_reader func,
	void *         farg,
	int            use_build_limit)
{
	char   *cp, *ep, linebuf[50];
	vint64 ttime, limit;
	long   taiof;
	struct calendar build;

	leapsec_clear(pt);
	if (use_build_limit && ntpcal_get_build_date(&build)) {
		/* don't prune everything -- permit the last 10yrs
		 * before build.
		 */
		build.year -= 10;
		limit = ntpcal_date_to_ntp64(&build);
	} else {
		memset(&limit, 0, sizeof(limit));
	}

	while (get_line(func, farg, linebuf, sizeof(linebuf))) {
		cp = linebuf;
		if (*cp == '#') {
			cp++;
			if (*cp == '@') {
				cp = skipws(cp+1);
				pt->head.expire = strtouv64(cp, &ep, 10);
				if (parsefail(cp, ep))
					goto fail_read;
				pt->lsig.etime = pt->head.expire.D_s.lo;
			} else if (*cp == '$') {
				cp = skipws(cp+1);
				pt->head.update = strtouv64(cp, &ep, 10);
				if (parsefail(cp, ep))
					goto fail_read;
			}
		} else if (isdigit((u_char)*cp)) {
			ttime = strtouv64(cp, &ep, 10);
			if (parsefail(cp, ep))
				goto fail_read;
			cp = skipws(ep);
			taiof = strtol(cp, &ep, 10);
			if (   parsefail(cp, ep)
			    || taiof > SHRT_MAX || taiof < SHRT_MIN)
				goto fail_read;
			if (ucmpv64(&ttime, &limit) >= 0) {
				if (!leapsec_raw(pt, &ttime,
						 taiof, FALSE))
					goto fail_insn;
			} else {
				pt->head.base_tai = (int16_t)taiof;
			}
			pt->lsig.ttime = ttime.D_s.lo;
			pt->lsig.taiof = (int16_t)taiof;
		}
	}
	return TRUE;

fail_read:
	errno = EILSEQ;
fail_insn:
	leapsec_clear(pt);
	return FALSE;
}

/* ---------------------------------------------------------------------
 * Dump a table in human-readable format. Use 'fprintf' and a FILE
 * pointer if you want to get it printed into a stream.
 */
void
leapsec_dump(
	const leap_table_t * pt  ,
	leapsec_dumper       func,
	void *               farg)
{
	int             idx;
	vint64          ts;
	struct calendar atb, ttb;

	ntpcal_ntp64_to_date(&ttb, &pt->head.expire);
	(*func)(farg, "leap table (%u entries) expires at %04u-%02u-%02u:\n",
		pt->head.size,
		ttb.year, ttb.month, ttb.monthday);
	idx = pt->head.size;
	while (idx-- != 0) {
		ts = pt->info[idx].ttime;
		ntpcal_ntp64_to_date(&ttb, &ts);
		ts = subv64u32(&ts, pt->info[idx].stime);
		ntpcal_ntp64_to_date(&atb, &ts);

		(*func)(farg, "%04u-%02u-%02u [%c] (%04u-%02u-%02u) - %d\n",
			ttb.year, ttb.month, ttb.monthday,
			"-*"[pt->info[idx].dynls != 0],
			atb.year, atb.month, atb.monthday,
			pt->info[idx].taiof);
	}
}

/* =====================================================================
 * usecase driven API functions
 */

int/*BOOL*/
leapsec_query(
	leap_result_t * qr   ,
	uint32_t        ts32 ,
	const time_t *  pivot)
{
	leap_table_t *   pt;
	vint64           ts64, last, next;
	uint32_t         due32;
	int              fired;

	/* preset things we use later on... */
	fired = FALSE;
	ts64  = ntpcal_ntp_to_ntp(ts32, pivot);
	pt    = leapsec_get_table(FALSE);
	memset(qr, 0, sizeof(leap_result_t));

	if (ucmpv64(&ts64, &pt->head.ebase) < 0) {
		/* Most likely after leap frame reset. Could also be a
		 * backstep of the system clock. Anyway, get the new
		 * leap era frame.
		 */
		reload_limits(pt, &ts64);
	} else if (ucmpv64(&ts64, &pt->head.dtime) >= 0) {
		/* Boundary crossed in forward direction. This might
		 * indicate a leap transition, so we prepare for that
		 * case.
		 *
		 * Some operations below are actually NOPs in electric
		 * mode, but having only one code path that works for
		 * both modes is easier to maintain.
		 *
		 * There's another quirk we must keep looking out for:
		 * If we just stepped the clock, the step might have
		 * crossed a leap boundary. As with backward steps, we
		 * do not want to raise the 'fired' event in that case.
		 * So we raise the 'fired' event only if we're close to
		 * the transition and just reload the limits otherwise.
		 */
		last = addv64i32(&pt->head.dtime, 3); /* get boundary */
		if (ucmpv64(&ts64, &last) >= 0) {
			/* that was likely a query after a step */
			reload_limits(pt, &ts64);
		} else {
			/* close enough for deeper examination */
			last = pt->head.ttime;
			qr->warped = (int16_t)(last.D_s.lo -
					       pt->head.dtime.D_s.lo);
			next = addv64i32(&ts64, qr->warped);
			reload_limits(pt, &next);
			fired = ucmpv64(&pt->head.ebase, &last) == 0;
			if (fired) {
				ts64 = next;
				ts32 = next.D_s.lo;
			} else {
				qr->warped = 0;
			}
		}
	}

	qr->tai_offs = pt->head.this_tai;
	qr->ebase    = pt->head.ebase;
	qr->ttime    = pt->head.ttime;

	/* If before the next scheduling alert, we're done. */
	if (ucmpv64(&ts64, &pt->head.stime) < 0)
		return fired;

	/* now start to collect the remaining data */
	due32 = pt->head.dtime.D_s.lo;

	qr->tai_diff  = pt->head.next_tai - pt->head.this_tai;
	qr->ddist     = due32 - ts32;
	qr->dynamic   = pt->head.dynls;
	qr->proximity = LSPROX_SCHEDULE;

	/* if not in the last day before transition, we're done. */
	if (!betweenu32(due32 - SECSPERDAY, ts32, due32))
		return fired;

	qr->proximity = LSPROX_ANNOUNCE;
	if (!betweenu32(due32 - 10, ts32, due32))
		return fired;

	/* The last 10s before the transition. Prepare for action! */
	qr->proximity = LSPROX_ALERT;
	return fired;
}

/* ------------------------------------------------------------------ */
int/*BOOL*/
leapsec_query_era(
	leap_era_t *   qr   ,
	uint32_t       ntpts,
	const time_t * pivot)
{
	const leap_table_t * pt;
	vint64               ts64;

	pt   = leapsec_get_table(FALSE);
	ts64 = ntpcal_ntp_to_ntp(ntpts, pivot);
	fetch_leap_era(qr, pt, &ts64);
	return TRUE;
}

/* ------------------------------------------------------------------ */
int/*BOOL*/
leapsec_frame(
        leap_result_t *qr)
{
	const leap_table_t * pt;

        memset(qr, 0, sizeof(leap_result_t));
	pt = leapsec_get_table(FALSE);

	qr->tai_offs = pt->head.this_tai;
	qr->tai_diff = pt->head.next_tai - pt->head.this_tai;
	qr->ebase    = pt->head.ebase;
	qr->ttime    = pt->head.ttime;
	qr->dynamic  = pt->head.dynls;

	return ucmpv64(&pt->head.ttime, &pt->head.stime) >= 0;
}

/* ------------------------------------------------------------------ */
/* Reset the current leap frame */
void
leapsec_reset_frame(void)
{
	reset_times(leapsec_get_table(FALSE));
}

/* ------------------------------------------------------------------ */
/* load a file from a FILE pointer. Note: If hcheck is true, load
 * only after successful signature check. The stream must be seekable
 * or this will fail.
 */
int/*BOOL*/
leapsec_load_stream(
	FILE       * ifp  ,
	const char * fname,
	int/*BOOL*/  logall)
{
	leap_table_t *pt;
	int           rcheck;

	if (NULL == fname)
		fname = "<unknown>";

	rcheck = leapsec_validate((leapsec_reader)getc, ifp);
	if (logall)
		switch (rcheck)
		{
		case LSVALID_GOODHASH:
			msyslog(LOG_NOTICE, "%s ('%s'): good hash signature",
				logPrefix, fname);
			break;

		case LSVALID_NOHASH:
			msyslog(LOG_ERR, "%s ('%s'): no hash signature",
				logPrefix, fname);
			break;
		case LSVALID_BADHASH:
			msyslog(LOG_ERR, "%s ('%s'): signature mismatch",
				logPrefix, fname);
			break;
		case LSVALID_BADFORMAT:
			msyslog(LOG_ERR, "%s ('%s'): malformed hash signature",
				logPrefix, fname);
			break;
		default:
			msyslog(LOG_ERR, "%s ('%s'): unknown error code %d",
				logPrefix, fname, rcheck);
			break;
		}
	if (rcheck < 0)
		return FALSE;

	rewind(ifp);
	pt = leapsec_get_table(TRUE);
	if (!leapsec_load(pt, (leapsec_reader)getc, ifp, TRUE)) {
		switch (errno) {
		case EINVAL:
			msyslog(LOG_ERR, "%s ('%s'): bad transition time",
				logPrefix, fname);
			break;
		case ERANGE:
			msyslog(LOG_ERR, "%s ('%s'): times not ascending",
				logPrefix, fname);
			break;
		default:
			msyslog(LOG_ERR, "%s ('%s'): parsing error",
				logPrefix, fname);
			break;
		}
		return FALSE;
	}

	if (pt->head.size)
		msyslog(LOG_NOTICE, "%s ('%s'): loaded, expire=%s last=%s ofs=%d",
			logPrefix, fname, lstostr(&pt->head.expire),
			lstostr(&pt->info[0].ttime), pt->info[0].taiof);
	else
		msyslog(LOG_NOTICE,
			"%s ('%s'): loaded, expire=%s ofs=%d (no entries after build date)",
			logPrefix, fname, lstostr(&pt->head.expire),
			pt->head.base_tai);

	return leapsec_set_table(pt);
}

/* ------------------------------------------------------------------ */
int/*BOOL*/
leapsec_load_file(
	const char  * fname,
	struct stat * sb_old,
	int/*BOOL*/   force,
	int/*BOOL*/   logall)
{
	FILE       * fp;
	struct stat  sb_new;
	int          rc;

	/* just do nothing if there is no leap file */
	if ( !(fname && *fname) )
		return FALSE;

	/* try to stat the leapfile */
	if (0 != stat(fname, &sb_new)) {
		if (logall)
			msyslog(LOG_ERR, "%s ('%s'): stat failed: %m",
				logPrefix, fname);
		return FALSE;
	}

	/* silently skip to postcheck if no new file found */
	if (NULL != sb_old) {
		if (!force
		 && sb_old->st_mtime == sb_new.st_mtime
		 && sb_old->st_ctime == sb_new.st_ctime
		   )
			return FALSE;
		*sb_old = sb_new;
	}

	/* try to open the leap file, complain if that fails
	 *
	 * [perlinger@ntp.org]
	 * coverity raises a TOCTOU (time-of-check/time-of-use) issue
	 * here, which is not entirely helpful: While there is indeed a
	 * possible race condition between the 'stat()' call above and
	 * the 'fopen)' call below, I intentionally want to omit the
	 * overhead of opening the file and calling 'fstat()', because
	 * in most cases the file would have be to closed anyway without
	 * reading the contents.  I chose to disable the coverity
	 * warning instead.
	 *
	 * So unless someone comes up with a reasonable argument why
	 * this could be a real issue, I'll just try to silence coverity
	 * on that topic.
	 */
	/* coverity[toctou] */
	if ((fp = fopen(fname, "r")) == NULL) {
		if (logall)
			msyslog(LOG_ERR,
				"%s ('%s'): open failed: %m",
				logPrefix, fname);
		return FALSE;
	}

	rc = leapsec_load_stream(fp, fname, logall);
	fclose(fp);
	return rc;
}

/* ------------------------------------------------------------------ */
void
leapsec_getsig(
	leap_signature_t * psig)
{
	const leap_table_t * pt;

	pt = leapsec_get_table(FALSE);
	memcpy(psig, &pt->lsig, sizeof(leap_signature_t));
}

/* ------------------------------------------------------------------ */
int/*BOOL*/
leapsec_expired(
	uint32_t       when,
	const time_t * tpiv)
{
	const leap_table_t * pt;
	vint64 limit;

	pt = leapsec_get_table(FALSE);
	limit = ntpcal_ntp_to_ntp(when, tpiv);
	return ucmpv64(&limit, &pt->head.expire) >= 0;
}

/* ------------------------------------------------------------------ */
int32_t
leapsec_daystolive(
	uint32_t       when,
	const time_t * tpiv)
{
	const leap_table_t * pt;
	vint64 limit;

	pt = leapsec_get_table(FALSE);
	limit = ntpcal_ntp_to_ntp(when, tpiv);
	limit = subv64(&pt->head.expire, &limit);
	return ntpcal_daysplit(&limit).hi;
}

/* ------------------------------------------------------------------ */
#if 0 /* currently unused -- possibly revived later */
int/*BOOL*/
leapsec_add_fix(
	int            total,
	uint32_t       ttime,
	uint32_t       etime,
	const time_t * pivot)
{
	time_t         tpiv;
	leap_table_t * pt;
	vint64         tt64, et64;

	if (pivot == NULL) {
		time(&tpiv);
		pivot = &tpiv;
	}

	et64 = ntpcal_ntp_to_ntp(etime, pivot);
	tt64 = ntpcal_ntp_to_ntp(ttime, pivot);
	pt   = leapsec_get_table(TRUE);

	if (   ucmpv64(&et64, &pt->head.expire) <= 0
	   || !leapsec_raw(pt, &tt64, total, FALSE) )
		return FALSE;

	pt->lsig.etime = etime;
	pt->lsig.ttime = ttime;
	pt->lsig.taiof = (int16_t)total;

	pt->head.expire = et64;

	return leapsec_set_table(pt);
}
#endif

/* ------------------------------------------------------------------ */
int/*BOOL*/
leapsec_add_dyn(
	int            insert,
	uint32_t       ntpnow,
	const time_t * pivot )
{
	leap_table_t * pt;
	vint64         now64;

	pt = leapsec_get_table(TRUE);
	now64 = ntpcal_ntp_to_ntp(ntpnow, pivot);
	return (   leapsec_add(pt, &now64, (insert != 0))
		&& leapsec_set_table(pt));
}

/* ------------------------------------------------------------------ */
int/*BOOL*/
leapsec_autokey_tai(
	int            tai_offset,
	uint32_t       ntpnow    ,
	const time_t * pivot     )
{
	leap_table_t * pt;
	leap_era_t     era;
	vint64         now64;
	int            idx;

	(void)tai_offset;
	pt = leapsec_get_table(FALSE);

	/* Bail out if the basic offset is not zero and the putative
	 * offset is bigger than 10s. That was in 1972 -- we don't want
	 * to go back that far!
	 */
	if (pt->head.base_tai != 0 || tai_offset < 10)
		return FALSE;

	/* If there's already data in the table, check if an update is
	 * possible. Update is impossible if there are static entries
	 * (since this indicates a valid leapsecond file) or if we're
	 * too close to a leapsecond transition: We do not know on what
	 * side the transition the sender might have been, so we use a
	 * dead zone around the transition.
	 */

	/* Check for static entries */
	for (idx = 0; idx != pt->head.size; idx++)
		if ( ! pt->info[idx].dynls)
			return FALSE;

	/* get the fulll time stamp and leap era for it */
	now64 = ntpcal_ntp_to_ntp(ntpnow, pivot);
	fetch_leap_era(&era, pt, &now64);

	/* check the limits with 20s dead band */
	era.ebase = addv64i32(&era.ebase,  20);
	if (ucmpv64(&now64, &era.ebase) < 0)
		return FALSE;

	era.ttime = addv64i32(&era.ttime, -20);
	if (ucmpv64(&now64, &era.ttime) > 0)
		return FALSE;

	/* Here we can proceed. Calculate the delta update. */
	tai_offset -= era.taiof;

	/* Shift the header info offsets. */
	pt->head.base_tai += tai_offset;
	pt->head.this_tai += tai_offset;
	pt->head.next_tai += tai_offset;

	/* Shift table entry offsets (if any) */
	for (idx = 0; idx != pt->head.size; idx++)
		pt->info[idx].taiof += tai_offset;

	/* claim success... */
	return TRUE;
}


/* =====================================================================
 * internal helpers
 */

/* [internal] Reset / init the time window in the leap processor to
 * force reload on next query. Since a leap transition cannot take place
 * at an odd second, the value chosen avoids spurious leap transition
 * triggers. Making all three times equal forces a reload. Using the
 * maximum value for unsigned 64 bits makes finding the next leap frame
 * a bit easier.
 */
static void
reset_times(
	leap_table_t * pt)
{
	memset(&pt->head.ebase, 0xFF, sizeof(vint64));
	pt->head.stime = pt->head.ebase;
	pt->head.ttime = pt->head.ebase;
	pt->head.dtime = pt->head.ebase;
}

/* [internal] Add raw data to the table, removing old entries on the
 * fly. This cannot fail currently.
 */
static int/*BOOL*/
add_range(
	leap_table_t *      pt,
	const leap_info_t * pi)
{
	/* If the table is full, make room by throwing out the oldest
	 * entry. But remember the accumulated leap seconds!
	 *
	 * Setting the first entry is a bit tricky, too: Simply assuming
	 * it is an insertion is wrong if the first entry is a dynamic
	 * leap second removal. So we decide on the sign -- if the first
	 * entry has a negative offset, we assume that it is a leap
	 * second removal. In both cases the table base offset is set
	 * accordingly to reflect the decision.
	 *
	 * In practice starting with a removal can only happen if the
	 * first entry is a dynamic request without having a leap file
	 * for the history proper.
	 */
	if (pt->head.size == 0) {
		if (pi->taiof >= 0)
			pt->head.base_tai = pi->taiof - 1;
		else
			pt->head.base_tai = pi->taiof + 1;
	} else if (pt->head.size >= MAX_HIST) {
		pt->head.size     = MAX_HIST - 1;
		pt->head.base_tai = pt->info[pt->head.size].taiof;
	}

	/* make room in lower end and insert item */
	memmove(pt->info+1, pt->info, pt->head.size*sizeof(*pt->info));
	pt->info[0] = *pi;
	pt->head.size++;

	/* invalidate the cached limit data -- we might have news ;-)
	 *
	 * This blocks a spurious transition detection. OTOH, if you add
	 * a value after the last query before a leap transition was
	 * expected to occur, this transition trigger is lost. But we
	 * can probably live with that.
	 */
	reset_times(pt);
	return TRUE;
}

/* [internal] given a reader function, read characters into a buffer
 * until either EOL or EOF is reached. Makes sure that the buffer is
 * always NUL terminated, but silently truncates excessive data. The
 * EOL-marker ('\n') is *not* stored in the buffer.
 *
 * Returns the pointer to the buffer, unless EOF was reached when trying
 * to read the first character of a line.
 */
static char *
get_line(
	leapsec_reader func,
	void *         farg,
	char *         buff,
	size_t         size)
{
	int   ch;
	char *ptr;

	/* if we cannot even store the delimiter, declare failure */
	if (buff == NULL || size == 0)
		return NULL;

	ptr = buff;
	while (EOF != (ch = (*func)(farg)) && '\n' != ch)
		if (size > 1) {
			size--;
			*ptr++ = (char)ch;
		}
	/* discard trailing whitespace */
	while (ptr != buff && isspace((u_char)ptr[-1]))
		ptr--;
	*ptr = '\0';
	return (ptr == buff && ch == EOF) ? NULL : buff;
}

/* [internal] skips whitespace characters from a character buffer. */
static char *
skipws(
	const char *ptr)
{
	while (isspace((u_char)*ptr))
		ptr++;
	return (char*)noconst(ptr);
}

/* [internal] check if a strtoXYZ ended at EOL or whitespace and
 * converted something at all. Return TRUE if something went wrong.
 */
static int/*BOOL*/
parsefail(
	const char * cp,
	const char * ep)
{
	return (cp == ep)
	    || (*ep && *ep != '#' && !isspace((u_char)*ep));
}

/* [internal] reload the table limits around the given time stamp. This
 * is where the real work is done when it comes to table lookup and
 * evaluation. Some care has been taken to have correct code for dealing
 * with boundary conditions and empty tables.
 *
 * In electric mode, transition and trip time are the same. In dumb
 * mode, the difference of the TAI offsets must be taken into account
 * and trip time and transition time become different. The difference
 * becomes the warping distance when the trip time is reached.
 */
static void
reload_limits(
	leap_table_t * pt,
	const vint64 * ts)
{
	int idx;

	/* Get full time and search the true lower bound. Use a
	 * simple loop here, since the number of entries does
	 * not warrant a binary search. This also works for an empty
	 * table, so there is no shortcut for that case.
	 */
	for (idx = 0; idx != pt->head.size; idx++)
		if (ucmpv64(ts, &pt->info[idx].ttime) >= 0)
			break;

	/* get time limits with proper bound conditions. Note that the
	 * bounds of the table will be observed even if the table is
	 * empty -- no undefined condition must arise from this code.
	 */
	if (idx >= pt->head.size) {
		memset(&pt->head.ebase, 0x00, sizeof(vint64));
		pt->head.this_tai = pt->head.base_tai;
	} else {
		pt->head.ebase    = pt->info[idx].ttime;
		pt->head.this_tai = pt->info[idx].taiof;
	}
	if (--idx >= 0) {
		pt->head.next_tai = pt->info[idx].taiof;
		pt->head.dynls    = pt->info[idx].dynls;
		pt->head.ttime    = pt->info[idx].ttime;

		if (_electric)
			pt->head.dtime = pt->head.ttime;
                else
			pt->head.dtime = addv64i32(
				&pt->head.ttime,
				pt->head.next_tai - pt->head.this_tai);

		pt->head.stime = subv64u32(
			&pt->head.ttime, pt->info[idx].stime);

	} else {
		memset(&pt->head.ttime, 0xFF, sizeof(vint64));
		pt->head.stime    = pt->head.ttime;
		pt->head.dtime    = pt->head.ttime;
		pt->head.next_tai = pt->head.this_tai;
		pt->head.dynls    = 0;
	}
}

/* [internal] fetch the leap era for a given time stamp.
 * This is a cut-down version the algorithm used to reload the table
 * limits, but it does not update any global state and provides just the
 * era information for a given time stamp.
 */
static void
fetch_leap_era(
	leap_era_t         * into,
	const leap_table_t * pt  ,
	const vint64       * ts  )
{
	int idx;

	/* Simple search loop, also works with empty table. */
	for (idx = 0; idx != pt->head.size; idx++)
		if (ucmpv64(ts, &pt->info[idx].ttime) >= 0)
			break;
	/* fetch era data, keeping an eye on boundary conditions */
	if (idx >= pt->head.size) {
		memset(&into->ebase, 0x00, sizeof(vint64));
		into->taiof = pt->head.base_tai;
	} else {
		into->ebase = pt->info[idx].ttime;
		into->taiof = pt->info[idx].taiof;
	}
	if (--idx >= 0)
		into->ttime = pt->info[idx].ttime;
	else
		memset(&into->ttime, 0xFF, sizeof(vint64));
}

/* [internal] Take a time stamp and create a leap second frame for
 * it. This will schedule a leap second for the beginning of the next
 * month, midnight UTC. The 'insert' argument tells if a leap second is
 * added (!=0) or removed (==0). We do not handle multiple inserts
 * (yet?)
 *
 * Returns 1 if the insert worked, 0 otherwise. (It's not possible to
 * insert a leap second into the current history -- only appending
 * towards the future is allowed!)
 */
static int/*BOOL*/
leapsec_add(
	leap_table_t*  pt    ,
	const vint64 * now64 ,
	int            insert)
{
	vint64		ttime, starttime;
	struct calendar	fts;
	leap_info_t	li;

	/* Check against the table expiration and the latest available
	 * leap entry. Do not permit inserts, only appends, and only if
	 * the extend the table beyond the expiration!
	 */
	if (   ucmpv64(now64, &pt->head.expire) < 0
	    || (pt->head.size && ucmpv64(now64, &pt->info[0].ttime) <= 0)) {
		errno = ERANGE;
		return FALSE;
	}

	ntpcal_ntp64_to_date(&fts, now64);
	/* To guard against dangling leap flags: do not accept leap
	 * second request on the 1st hour of the 1st day of the month.
	 */
	if (fts.monthday == 1 && fts.hour == 0) {
		errno = EINVAL;
		return FALSE;
	}

	/* Ok, do the remaining calculations */
	fts.monthday = 1;
	fts.hour     = 0;
	fts.minute   = 0;
	fts.second   = 0;
	starttime = ntpcal_date_to_ntp64(&fts);
	fts.month++;
	ttime = ntpcal_date_to_ntp64(&fts);

	li.ttime = ttime;
	li.stime = ttime.D_s.lo - starttime.D_s.lo;
	li.taiof = (pt->head.size ? pt->info[0].taiof : pt->head.base_tai)
	         + (insert ? 1 : -1);
	li.dynls = 1;
	return add_range(pt, &li);
}

/* [internal] Given a time stamp for a leap insertion (the exact begin
 * of the new leap era), create new leap frame and put it into the
 * table. This is the work horse for reading a leap file and getting a
 * leap second update via authenticated network packet.
 */
int/*BOOL*/
leapsec_raw(
	leap_table_t * pt,
	const vint64 * ttime,
	int            taiof,
	int            dynls)
{
	vint64		starttime;
	struct calendar	fts;
	leap_info_t	li;

	/* Check that we either extend the table or get a duplicate of
	 * the latest entry. The latter is a benevolent overwrite with
	 * identical data and could happen if we get an autokey message
	 * that extends the lifetime of the current leapsecond table.
	 * Otherwise paranoia rulez!
	 */
	if (pt->head.size) {
		int cmp = ucmpv64(ttime, &pt->info[0].ttime);
		if (cmp == 0)
			cmp -= (taiof != pt->info[0].taiof);
		if (cmp < 0) {
			errno = ERANGE;
			return FALSE;
		}
		if (cmp == 0)
			return TRUE;
	}

	ntpcal_ntp64_to_date(&fts, ttime);
	/* If this does not match the exact month start, bail out. */
	if (fts.monthday != 1 || fts.hour || fts.minute || fts.second) {
		errno = EINVAL;
		return FALSE;
	}
	fts.month--; /* was in range 1..12, no overflow here! */
	starttime = ntpcal_date_to_ntp64(&fts);
	li.ttime = *ttime;
	li.stime = ttime->D_s.lo - starttime.D_s.lo;
	li.taiof = (int16_t)taiof;
	li.dynls = (dynls != 0);
	return add_range(pt, &li);
}

/* [internal] Do a wrap-around save range inclusion check.
 * Returns TRUE if x in [lo,hi[ (intervall open on right side) with full
 * handling of an overflow / wrap-around.
 */
static int/*BOOL*/
betweenu32(
	uint32_t lo,
	uint32_t x,
	uint32_t hi)
{
	int rc;

	if (lo <= hi)
		rc = (lo <= x) && (x < hi);
	else
		rc = (lo <= x) || (x < hi);
	return rc;
}

/* =====================================================================
 * validation stuff
 */

typedef struct {
	unsigned char hv[ISC_SHA1_DIGESTLENGTH];
} sha1_digest;

/* [internal] parse a digest line to get the hash signature
 * The NIST code creating the hash writes them out as 5 hex integers
 * without leading zeros. This makes reading them back as hex-encoded
 * BLOB impossible, because there might be less than 40 hex digits.
 *
 * The solution is to read the values back as integers, and then do the
 * byte twiddle necessary to get it into an array of 20 chars. The
 * drawback is that it permits any acceptable number syntax provided by
 * 'scanf()' and 'strtoul()', including optional signs and '0x'
 * prefixes.
 */
static int/*BOOL*/
do_leap_hash(
	sha1_digest * mac,
	char const  * cp )
{
	int wi, di, num, len;
	unsigned long tmp[5];

	memset(mac, 0, sizeof(*mac));
	num = sscanf(cp, " %lx %lx %lx %lx %lx%n",
		     &tmp[0], &tmp[1], &tmp[2], &tmp[3], &tmp[4],
		     &len);
	if (num != 5 || cp[len] > ' ')
		return FALSE;

	/* now do the byte twiddle */
	for (wi=0; wi < 5; ++wi)
		for (di=3; di >= 0; --di) {
			mac->hv[wi*4 + di] =
				(unsigned char)(tmp[wi] & 0x0FF);
			tmp[wi] >>= 8;
		}
	return TRUE;
}

/* [internal] add the digits of a data line to the hash, stopping at the
 * next hash ('#') character.
 */
static void
do_hash_data(
	isc_sha1_t * mdctx,
	char const * cp   )
{
	unsigned char  text[32]; // must be power of two!
	unsigned int   tlen =  0;
	unsigned char  ch;

	while ('\0' != (ch = *cp++) && '#' != ch)
		if (isdigit(ch)) {
			text[tlen++] = ch;
			tlen &= (sizeof(text)-1);
			if (0 == tlen)
				isc_sha1_update(
					mdctx, text, sizeof(text));
		}

	if (0 < tlen)
		isc_sha1_update(mdctx, text, tlen);
}

/* given a reader and a reader arg, calculate and validate the the hash
 * signature of a NIST leap second file.
 */
int
leapsec_validate(
	leapsec_reader func,
	void *         farg)
{
	isc_sha1_t     mdctx;
	sha1_digest    rdig, ldig; /* remote / local digests */
	char           line[50];
	int            hlseen = -1;

	isc_sha1_init(&mdctx);
	while (get_line(func, farg, line, sizeof(line))) {
		if (!strncmp(line, "#h", 2))
			hlseen = do_leap_hash(&rdig, line+2);
		else if (!strncmp(line, "#@", 2))
			do_hash_data(&mdctx, line+2);
		else if (!strncmp(line, "#$", 2))
			do_hash_data(&mdctx, line+2);
		else if (isdigit((unsigned char)line[0]))
			do_hash_data(&mdctx, line);
	}
	isc_sha1_final(&mdctx, ldig.hv);
	isc_sha1_invalidate(&mdctx);

	if (0 > hlseen)
		return LSVALID_NOHASH;
	if (0 == hlseen)
		return LSVALID_BADFORMAT;
	if (0 != memcmp(&rdig, &ldig, sizeof(sha1_digest)))
		return LSVALID_BADHASH;
	return LSVALID_GOODHASH;
}

/*
 * lstostr - prettyprint NTP seconds
 */
static const char *
lstostr(
	const vint64 * ts)
{
	char *		buf;
	struct calendar tm;

	LIB_GETBUF(buf);

	if ( ! (ts->d_s.hi >= 0 && ntpcal_ntp64_to_date(&tm, ts) >= 0))
		snprintf(buf, LIB_BUFLENGTH, "%s", "9999-12-31T23:59:59Z");
	else
		snprintf(buf, LIB_BUFLENGTH, "%04d-%02d-%02dT%02d:%02d:%02dZ",
			tm.year, tm.month, tm.monthday,
			tm.hour, tm.minute, tm.second);

	return buf;
}

/* reset the global state for unit tests */
void
leapsec_ut_pristine(void)
{
	memset(_ltab, 0, sizeof(_ltab));
	_lptr     = NULL;
	_electric = 0;
}



/* -*- that's all folks! -*- */
