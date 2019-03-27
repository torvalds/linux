/*
 * ntp_leapsec.h - leap second processing for NTPD
 *
 * Written by Juergen Perlinger (perlinger@ntp.org) for the NTP project.
 * The contents of 'html/copyright.html' apply.
 * ----------------------------------------------------------------------
 * This is an attempt to get the leap second handling into a dedicated
 * module to make the somewhat convoluted logic testable.
 */

#ifndef NTP_LEAPSEC_H
#define NTP_LEAPSEC_H

struct stat;


/* function pointer types. Note that 'fprintf' and 'getc' can be casted
 * to the dumper resp. reader type, provided the auxiliary argument is a
 * valid FILE pointer in hat case.
 */
typedef void (*leapsec_dumper)(void*, const char *fmt, ...);
typedef int  (*leapsec_reader)(void*);

struct leap_table;
typedef struct leap_table leap_table_t;

/* Validate a stream containing a leap second file in the NIST / NTPD
 * format that can also be loaded via 'leapsec_load()'. This uses
 * the SHA1 hash and preprocessing as described in the NIST leapsecond
 * file.
 */
#define LSVALID_GOODHASH	1	/* valid signature         */
#define LSVALID_NOHASH		0	/* no signature in file    */
#define LSVALID_BADHASH	       -1	/* signature mismatch      */
#define LSVALID_BADFORMAT      -2	/* signature not parseable */

extern int leapsec_validate(leapsec_reader, void*);


/* Set/get electric mode
 * Electric mode is defined as the operation mode where the system clock
 * automagically manages the leap second, so we don't have to care about
 * stepping the clock. (This should be the case with most systems,
 * including the current implementation of the Win32 timekeeping.)
 *
 * The consequence of electric mode is that we do not 'see' the leap
 * second, and no client actions are needed when crossing the leap era
 * boundary.  In manual (aka non-electric) mode the clock will simply
 * step forward untill *we* (that is, this module) tells the client app
 * to step at the right time. This needs a slightly different type of
 * processing, so switching between those two modes should not be done
 * too close to a leap second. The transition might be lost in that
 * case. (The limit is actual 2 sec before transition.)
 *
 * OTOH, this is a system characteristic, so it's expected to be set
 * properly somewhere after system start and retain the value.
 *
 * Simply querying the state or setting it to the same value as before
 * does not have any unwanted side effects.  You can query by giving a
 * negative value for the switch.
 */
extern int/*BOOL*/ leapsec_electric(int/*BOOL*/ on);

/* Query result for a leap era. This is the minimal stateless
 * information available for a time stamp in UTC.
 */
struct leap_era {
	vint64   ebase;	/* era base (UTC of start)		*/
	vint64   ttime; /* era end (UTC of next leap second)	*/
	int16_t  taiof;	/* offset to TAI in this era		*/
};
typedef struct leap_era leap_era_t;

/* Query result for a leap second schedule
 * 'ebase' is the nominal UTC time when the current leap era
 *      started. (Era base time)
 * 'ttime' is the next transition point in full time scale. (Nominal UTC
 *      time when the next leap era starts.)
 * 'ddist' is the distance to the transition, in clock seconds.
 *      This is the distance to the due time, which is different
 *      from the transition time if the mode is non-electric.
 *	Only valid if 'tai_diff' is not zero.
 * 'tai_offs' is the CURRENT distance from clock (UTC) to TAI. Always
 *      valid.
 * 'tai_diff' is the change in TAI offset after the next leap
 *	transition. Zero if nothing is pending or too far ahead.
 * 'warped' is set only once, when the the leap second occurred between
 *	two queries. Always zero in electric mode. If non-zero,
 *      immediately step the clock.
 * 'proximity' is a proximity warning. See definitions below. This is
 *	more useful than an absolute difference to the leap second.
 * 'dynamic' != 0 if entry was requested by clock/peer
 */
struct leap_result {
	vint64   ebase;
	vint64   ttime;
	uint32_t ddist;
	int16_t  tai_offs;
	int16_t  tai_diff;
	int16_t  warped;
	uint8_t  proximity;
	uint8_t  dynamic;
};
typedef struct leap_result leap_result_t;

/* The leap signature is used in two distinct circumstances, and it has
 * slightly different content in these cases:
 *  - it is used to indictae the time range covered by the leap second
 *    table, and then it contains the last transition, TAI offset after
 *    the final transition, and the expiration time.
 *  - it is used to query data for AUTOKEY updates, and then it contains
 *    the *current* TAI offset, the *next* transition time and the
 *    expiration time of the table.
 */
struct leap_signature {
	uint32_t etime;	/* expiration time	*/
	uint32_t ttime;	/* transition time	*/
	int16_t  taiof;	/* total offset to TAI	*/
};
typedef struct leap_signature leap_signature_t;


#ifdef LEAP_SMEAR

struct leap_smear_info {
	int enabled;        /* not 0 if smearing is generally enabled */
	int in_progress;    /* not 0 if smearing is in progress, i.e. the offset has been computed */
	int leap_occurred;  /* not 0 if the leap second has already occurred, i.e., during the leap second */
	double doffset;     /* the current smear offset as double */
	l_fp offset;        /* the current smear offset */
	uint32_t t_offset;  /* the current time for which a smear offset has been computed */
	long interval;      /* smear interval, in [s], should be at least some hours */
	double intv_start;  /* start time of the smear interval */
	double intv_end;    /* end time of the smear interval */
};
typedef struct leap_smear_info leap_smear_info_t;

#endif  /* LEAP_SMEAR */


#define LSPROX_NOWARN	0	/* clear radar screen         */
#define LSPROX_SCHEDULE	1	/* less than 1 month to target*/
#define LSPROX_ANNOUNCE	2	/* less than 1 day to target  */
#define LSPROX_ALERT	3	/* less than 10 sec to target */

/* Get the current or alternate table pointer. Getting the alternate
 * pointer will automatically copy the primary table, so it can be
 * subsequently modified.
 */
extern leap_table_t *leapsec_get_table(int alternate);

/* Set the current leap table. Accepts only return values from
 * 'leapsec_get_table()', so it's hard to do something wrong. Returns
 * TRUE if the current table is the requested one.
 */
extern int/*BOOL*/ leapsec_set_table(leap_table_t *);

/* Clear all leap second data. Use it for init & cleanup */
extern void leapsec_clear(leap_table_t*);

/* Load a leap second file. If 'blimit' is set, do not store (but
 * register with their TAI offset) leap entries before the build date.
 * Update the leap signature data on the fly.
 */
extern int/*BOOL*/ leapsec_load(leap_table_t*, leapsec_reader,
				void*, int blimit);

/* Dump the current leap table in readable format, using the provided
 * dump formatter function.
 */
extern void leapsec_dump(const leap_table_t*, leapsec_dumper func, void *farg);

/* Read a leap second file from stream. This is a convenience wrapper
 * around the generic load function, 'leapsec_load()'.
 */
extern int/*BOOL*/ leapsec_load_stream(FILE * fp, const char * fname,
				       int/*BOOL*/logall);

/* Read a leap second file from file. It checks that the file exists and
 * (if 'force' is not applied) the ctime/mtime has changed since the
 * last load. If the file has to be loaded, either due to 'force' or
 * changed time stamps, the 'stat()' results of the file are stored in
 * '*sb' for the next cycle. Returns TRUE on successful load, FALSE
 * otherwise. Uses 'leapsec_load_stream()' internally.
 */
extern int/*BOOL*/ leapsec_load_file(const char * fname, struct stat * sb,
				     int/*BOOL*/force, int/*BOOL*/logall);

/* Get the current leap data signature. This consists of the last
 * ransition, the table expiration, and the total TAI difference at the
 * last transition. This is valid even if the leap transition itself was
 * culled due to the build date limit.
 */
extern void        leapsec_getsig(leap_signature_t * psig);

/* Check if the leap table is expired at the given time.
 */
extern int/*BOOL*/ leapsec_expired(uint32_t when, const time_t * pivot);

/* Get the distance to expiration in days.
 * Returns negative values if expired, zero if there are less than 24hrs
 * left, and positive numbers otherwise.
 */
extern int32_t leapsec_daystolive(uint32_t when, const time_t * pivot);

/* Reset the current leap frame, so the next query will do proper table
 * lookup from fresh. Suppresses a possible leap era transition detection
 * for the next query.
 */
extern void leapsec_reset_frame(void);

#if 0 /* currently unused -- possibly revived later */
/* Given a transition time, the TAI offset valid after that and an
 * expiration time, try to establish a system leap transition. Only
 * works if the existing table is extended. On success, updates the
 * signature data.
 */
extern int/*BOOL*/ leapsec_add_fix(int offset, uint32_t ttime, uint32_t etime,
				   const time_t * pivot);
#endif

/* Take a time stamp and create a leap second frame for it. This will
 * schedule a leap second for the beginning of the next month, midnight
 * UTC. The 'insert' argument tells if a leap second is added (!=0) or
 * removed (==0). We do not handle multiple inserts (yet?)
 *
 * Returns 1 if the insert worked, 0 otherwise. (It's not possible to
 * insert a leap second into the current history -- only appending
 * towards the future is allowed!)
 *
 * 'ntp_now' is subject to era unfolding. The entry is marked
 * dynamic. The leap signature is NOT updated.
 */
extern int/*BOOL*/ leapsec_add_dyn(int/*BOOL*/ insert, uint32_t ntp_now,
				   const time_t * pivot);

/* Take a time stamp and get the associated leap information. The time
 * stamp is subject to era unfolding around the pivot or the current
 * system time if pivot is NULL. Sets the information in '*qr' and
 * returns TRUE if a leap second era boundary was crossed between the
 * last and the current query. In that case, qr->warped contains the
 * required clock stepping, which is always zero in electric mode.
 */
extern int/*BOOL*/ leapsec_query(leap_result_t * qr, uint32_t ntpts,
				 const time_t * pivot);

/* For a given time stamp, fetch the data for the bracketing leap
 * era. The time stamp is subject to NTP era unfolding.
 */
extern int/*BOOL*/ leapsec_query_era(leap_era_t * qr, uint32_t ntpts,
				     const time_t * pivot);

/* Get the current leap frame info. Returns TRUE if the result contains
 * useable data, FALSE if there is currently no leap second frame.
 * This merely replicates some results from a previous query, but since
 * it does not check the current time, only the following entries are
 * meaningful:
 *  qr->ttime;
 *  qr->tai_offs;
 *  qr->tai_diff;
 *  qr->dynamic;
 */
extern int/*BOOL*/ leapsec_frame(leap_result_t *qr);


/* Process a AUTOKEY TAI offset information. This *might* augment the
 * current leap data table with the given TAI offset.
 * Returns TRUE if action was taken, FALSE otherwise.
 */
extern int/*BOOL*/ leapsec_autokey_tai(int tai_offset, uint32_t ntpnow,
				       const time_t * pivot);

/* reset global state for unit tests */
extern void leapsec_ut_pristine(void);

#endif /* !defined(NTP_LEAPSEC_H) */
