/*
 * This code is in the public domain and has no copyright.
 *
 * This is a plain C recursive-descent translation of an old
 * public-domain YACC grammar that has been used for parsing dates in
 * very many open-source projects.
 *
 * Since the original authors were generous enough to donate their
 * work to the public domain, I feel compelled to match their
 * generosity.
 *
 * Tim Kientzle, February 2009.
 */

/*
 * Header comment from original getdate.y:
 */

/*
**  Originally written by Steven M. Bellovin <smb@research.att.com> while
**  at the University of North Carolina at Chapel Hill.  Later tweaked by
**  a couple of people on Usenet.  Completely overhauled by Rich $alz
**  <rsalz@bbn.com> and Jim Berets <jberets@bbn.com> in August, 1990;
**
**  This grammar has 10 shift/reduce conflicts.
**
**  This code is in the public domain and has no copyright.
*/

#ifdef __FreeBSD__
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define __LIBARCHIVE_BUILD 1
#include "archive_getdate.h"

/* Basic time units. */
#define	EPOCH		1970
#define	MINUTE		(60L)
#define	HOUR		(60L * MINUTE)
#define	DAY		(24L * HOUR)

/* Daylight-savings mode:  on, off, or not yet known. */
enum DSTMODE { DSTon, DSToff, DSTmaybe };
/* Meridian:  am or pm. */
enum { tAM, tPM };
/* Token types returned by nexttoken() */
enum { tAGO = 260, tDAY, tDAYZONE, tAMPM, tMONTH, tMONTH_UNIT, tSEC_UNIT,
       tUNUMBER, tZONE, tDST };
struct token { int token; time_t value; };

/*
 * Parser state.
 */
struct gdstate {
	struct token *tokenp; /* Pointer to next token. */
	/* HaveXxxx counts how many of this kind of phrase we've seen;
	 * it's a fatal error to have more than one time, zone, day,
	 * or date phrase. */
	int	HaveYear;
	int	HaveMonth;
	int	HaveDay;
	int	HaveWeekDay; /* Day of week */
	int	HaveTime; /* Hour/minute/second */
	int	HaveZone; /* timezone and/or DST info */
	int	HaveRel; /* time offset; we can have more than one */
	/* Absolute time values. */
	time_t	Timezone;  /* Seconds offset from GMT */
	time_t	Day;
	time_t	Hour;
	time_t	Minutes;
	time_t	Month;
	time_t	Seconds;
	time_t	Year;
	/* DST selection */
	enum DSTMODE	DSTmode;
	/* Day of week accounting, e.g., "3rd Tuesday" */
	time_t	DayOrdinal; /* "3" in "3rd Tuesday" */
	time_t	DayNumber; /* "Tuesday" in "3rd Tuesday" */
	/* Relative time values: hour/day/week offsets are measured in
	 * seconds, month/year are counted in months. */
	time_t	RelMonth;
	time_t	RelSeconds;
};

/*
 * A series of functions that recognize certain common time phrases.
 * Each function returns 1 if it managed to make sense of some of the
 * tokens, zero otherwise.
 */

/*
 *  hour:minute or hour:minute:second with optional AM, PM, or numeric
 *  timezone offset
 */
static int
timephrase(struct gdstate *gds)
{
	if (gds->tokenp[0].token == tUNUMBER
	    && gds->tokenp[1].token == ':'
	    && gds->tokenp[2].token == tUNUMBER
	    && gds->tokenp[3].token == ':'
	    && gds->tokenp[4].token == tUNUMBER) {
		/* "12:14:18" or "22:08:07" */
		++gds->HaveTime;
		gds->Hour = gds->tokenp[0].value;
		gds->Minutes = gds->tokenp[2].value;
		gds->Seconds = gds->tokenp[4].value;
		gds->tokenp += 5;
	}
	else if (gds->tokenp[0].token == tUNUMBER
	    && gds->tokenp[1].token == ':'
	    && gds->tokenp[2].token == tUNUMBER) {
		/* "12:14" or "22:08" */
		++gds->HaveTime;
		gds->Hour = gds->tokenp[0].value;
		gds->Minutes = gds->tokenp[2].value;
		gds->Seconds = 0;
		gds->tokenp += 3;
	}
	else if (gds->tokenp[0].token == tUNUMBER
	    && gds->tokenp[1].token == tAMPM) {
		/* "7" is a time if it's followed by "am" or "pm" */
		++gds->HaveTime;
		gds->Hour = gds->tokenp[0].value;
		gds->Minutes = gds->Seconds = 0;
		/* We'll handle the AM/PM below. */
		gds->tokenp += 1;
	} else {
		/* We can't handle this. */
		return 0;
	}

	if (gds->tokenp[0].token == tAMPM) {
		/* "7:12pm", "12:20:13am" */
		if (gds->Hour == 12)
			gds->Hour = 0;
		if (gds->tokenp[0].value == tPM)
			gds->Hour += 12;
		gds->tokenp += 1;
	}
	if (gds->tokenp[0].token == '+'
	    && gds->tokenp[1].token == tUNUMBER) {
		/* "7:14+0700" */
		gds->HaveZone++;
		gds->DSTmode = DSToff;
		gds->Timezone = - ((gds->tokenp[1].value / 100) * HOUR
		    + (gds->tokenp[1].value % 100) * MINUTE);
		gds->tokenp += 2;
	}
	if (gds->tokenp[0].token == '-'
	    && gds->tokenp[1].token == tUNUMBER) {
		/* "19:14:12-0530" */
		gds->HaveZone++;
		gds->DSTmode = DSToff;
		gds->Timezone = + ((gds->tokenp[1].value / 100) * HOUR
		    + (gds->tokenp[1].value % 100) * MINUTE);
		gds->tokenp += 2;
	}
	return 1;
}

/*
 * Timezone name, possibly including DST.
 */
static int
zonephrase(struct gdstate *gds)
{
	if (gds->tokenp[0].token == tZONE
	    && gds->tokenp[1].token == tDST) {
		gds->HaveZone++;
		gds->Timezone = gds->tokenp[0].value;
		gds->DSTmode = DSTon;
		gds->tokenp += 1;
		return 1;
	}

	if (gds->tokenp[0].token == tZONE) {
		gds->HaveZone++;
		gds->Timezone = gds->tokenp[0].value;
		gds->DSTmode = DSToff;
		gds->tokenp += 1;
		return 1;
	}

	if (gds->tokenp[0].token == tDAYZONE) {
		gds->HaveZone++;
		gds->Timezone = gds->tokenp[0].value;
		gds->DSTmode = DSTon;
		gds->tokenp += 1;
		return 1;
	}
	return 0;
}

/*
 * Year/month/day in various combinations.
 */
static int
datephrase(struct gdstate *gds)
{
	if (gds->tokenp[0].token == tUNUMBER
	    && gds->tokenp[1].token == '/'
	    && gds->tokenp[2].token == tUNUMBER
	    && gds->tokenp[3].token == '/'
	    && gds->tokenp[4].token == tUNUMBER) {
		gds->HaveYear++;
		gds->HaveMonth++;
		gds->HaveDay++;
		if (gds->tokenp[0].value >= 13) {
			/* First number is big:  2004/01/29, 99/02/17 */
			gds->Year = gds->tokenp[0].value;
			gds->Month = gds->tokenp[2].value;
			gds->Day = gds->tokenp[4].value;
		} else if ((gds->tokenp[4].value >= 13)
		    || (gds->tokenp[2].value >= 13)) {
			/* Last number is big:  01/07/98 */
			/* Middle number is big:  01/29/04 */
			gds->Month = gds->tokenp[0].value;
			gds->Day = gds->tokenp[2].value;
			gds->Year = gds->tokenp[4].value;
		} else {
			/* No significant clues: 02/03/04 */
			gds->Month = gds->tokenp[0].value;
			gds->Day = gds->tokenp[2].value;
			gds->Year = gds->tokenp[4].value;
		}
		gds->tokenp += 5;
		return 1;
	}

	if (gds->tokenp[0].token == tUNUMBER
	    && gds->tokenp[1].token == '/'
	    && gds->tokenp[2].token == tUNUMBER) {
		/* "1/15" */
		gds->HaveMonth++;
		gds->HaveDay++;
		gds->Month = gds->tokenp[0].value;
		gds->Day = gds->tokenp[2].value;
		gds->tokenp += 3;
		return 1;
	}

	if (gds->tokenp[0].token == tUNUMBER
	    && gds->tokenp[1].token == '-'
	    && gds->tokenp[2].token == tUNUMBER
	    && gds->tokenp[3].token == '-'
	    && gds->tokenp[4].token == tUNUMBER) {
		/* ISO 8601 format.  yyyy-mm-dd.  */
		gds->HaveYear++;
		gds->HaveMonth++;
		gds->HaveDay++;
		gds->Year = gds->tokenp[0].value;
		gds->Month = gds->tokenp[2].value;
		gds->Day = gds->tokenp[4].value;
		gds->tokenp += 5;
		return 1;
	}

	if (gds->tokenp[0].token == tUNUMBER
	    && gds->tokenp[1].token == '-'
	    && gds->tokenp[2].token == tMONTH
	    && gds->tokenp[3].token == '-'
	    && gds->tokenp[4].token == tUNUMBER) {
		gds->HaveYear++;
		gds->HaveMonth++;
		gds->HaveDay++;
		if (gds->tokenp[0].value > 31) {
			/* e.g. 1992-Jun-17 */
			gds->Year = gds->tokenp[0].value;
			gds->Month = gds->tokenp[2].value;
			gds->Day = gds->tokenp[4].value;
		} else {
			/* e.g. 17-JUN-1992.  */
			gds->Day = gds->tokenp[0].value;
			gds->Month = gds->tokenp[2].value;
			gds->Year = gds->tokenp[4].value;
		}
		gds->tokenp += 5;
		return 1;
	}

	if (gds->tokenp[0].token == tMONTH
	    && gds->tokenp[1].token == tUNUMBER
	    && gds->tokenp[2].token == ','
	    && gds->tokenp[3].token == tUNUMBER) {
		/* "June 17, 2001" */
		gds->HaveYear++;
		gds->HaveMonth++;
		gds->HaveDay++;
		gds->Month = gds->tokenp[0].value;
		gds->Day = gds->tokenp[1].value;
		gds->Year = gds->tokenp[3].value;
		gds->tokenp += 4;
		return 1;
	}

	if (gds->tokenp[0].token == tMONTH
	    && gds->tokenp[1].token == tUNUMBER) {
		/* "May 3" */
		gds->HaveMonth++;
		gds->HaveDay++;
		gds->Month = gds->tokenp[0].value;
		gds->Day = gds->tokenp[1].value;
		gds->tokenp += 2;
		return 1;
	}

	if (gds->tokenp[0].token == tUNUMBER
	    && gds->tokenp[1].token == tMONTH
	    && gds->tokenp[2].token == tUNUMBER) {
		/* "12 Sept 1997" */
		gds->HaveYear++;
		gds->HaveMonth++;
		gds->HaveDay++;
		gds->Day = gds->tokenp[0].value;
		gds->Month = gds->tokenp[1].value;
		gds->Year = gds->tokenp[2].value;
		gds->tokenp += 3;
		return 1;
	}

	if (gds->tokenp[0].token == tUNUMBER
	    && gds->tokenp[1].token == tMONTH) {
		/* "12 Sept" */
		gds->HaveMonth++;
		gds->HaveDay++;
		gds->Day = gds->tokenp[0].value;
		gds->Month = gds->tokenp[1].value;
		gds->tokenp += 2;
		return 1;
	}

	return 0;
}

/*
 * Relative time phrase: "tomorrow", "yesterday", "+1 hour", etc.
 */
static int
relunitphrase(struct gdstate *gds)
{
	if (gds->tokenp[0].token == '-'
	    && gds->tokenp[1].token == tUNUMBER
	    && gds->tokenp[2].token == tSEC_UNIT) {
		/* "-3 hours" */
		gds->HaveRel++;
		gds->RelSeconds -= gds->tokenp[1].value * gds->tokenp[2].value;
		gds->tokenp += 3;
		return 1;
	}
	if (gds->tokenp[0].token == '+'
	    && gds->tokenp[1].token == tUNUMBER
	    && gds->tokenp[2].token == tSEC_UNIT) {
		/* "+1 minute" */
		gds->HaveRel++;
		gds->RelSeconds += gds->tokenp[1].value * gds->tokenp[2].value;
		gds->tokenp += 3;
		return 1;
	}
	if (gds->tokenp[0].token == tUNUMBER
	    && gds->tokenp[1].token == tSEC_UNIT) {
		/* "1 day" */
		gds->HaveRel++;
		gds->RelSeconds += gds->tokenp[0].value * gds->tokenp[1].value;
		gds->tokenp += 2;
		return 1;
	}
	if (gds->tokenp[0].token == '-'
	    && gds->tokenp[1].token == tUNUMBER
	    && gds->tokenp[2].token == tMONTH_UNIT) {
		/* "-3 months" */
		gds->HaveRel++;
		gds->RelMonth -= gds->tokenp[1].value * gds->tokenp[2].value;
		gds->tokenp += 3;
		return 1;
	}
	if (gds->tokenp[0].token == '+'
	    && gds->tokenp[1].token == tUNUMBER
	    && gds->tokenp[2].token == tMONTH_UNIT) {
		/* "+5 years" */
		gds->HaveRel++;
		gds->RelMonth += gds->tokenp[1].value * gds->tokenp[2].value;
		gds->tokenp += 3;
		return 1;
	}
	if (gds->tokenp[0].token == tUNUMBER
	    && gds->tokenp[1].token == tMONTH_UNIT) {
		/* "2 years" */
		gds->HaveRel++;
		gds->RelMonth += gds->tokenp[0].value * gds->tokenp[1].value;
		gds->tokenp += 2;
		return 1;
	}
	if (gds->tokenp[0].token == tSEC_UNIT) {
		/* "now", "tomorrow" */
		gds->HaveRel++;
		gds->RelSeconds += gds->tokenp[0].value;
		gds->tokenp += 1;
		return 1;
	}
	if (gds->tokenp[0].token == tMONTH_UNIT) {
		/* "month" */
		gds->HaveRel++;
		gds->RelMonth += gds->tokenp[0].value;
		gds->tokenp += 1;
		return 1;
	}
	return 0;
}

/*
 * Day of the week specification.
 */
static int
dayphrase(struct gdstate *gds)
{
	if (gds->tokenp[0].token == tDAY) {
		/* "tues", "wednesday," */
		gds->HaveWeekDay++;
		gds->DayOrdinal = 1;
		gds->DayNumber = gds->tokenp[0].value;
		gds->tokenp += 1;
		if (gds->tokenp[0].token == ',')
			gds->tokenp += 1;
		return 1;
	}
	if (gds->tokenp[0].token == tUNUMBER
		&& gds->tokenp[1].token == tDAY) {
		/* "second tues" "3 wed" */
		gds->HaveWeekDay++;
		gds->DayOrdinal = gds->tokenp[0].value;
		gds->DayNumber = gds->tokenp[1].value;
		gds->tokenp += 2;
		return 1;
	}
	return 0;
}

/*
 * Try to match a phrase using one of the above functions.
 * This layer also deals with a couple of generic issues.
 */
static int
phrase(struct gdstate *gds)
{
	if (timephrase(gds))
		return 1;
	if (zonephrase(gds))
		return 1;
	if (datephrase(gds))
		return 1;
	if (dayphrase(gds))
		return 1;
	if (relunitphrase(gds)) {
		if (gds->tokenp[0].token == tAGO) {
			gds->RelSeconds = -gds->RelSeconds;
			gds->RelMonth = -gds->RelMonth;
			gds->tokenp += 1;
		}
		return 1;
	}

	/* Bare numbers sometimes have meaning. */
	if (gds->tokenp[0].token == tUNUMBER) {
		if (gds->HaveTime && !gds->HaveYear && !gds->HaveRel) {
			gds->HaveYear++;
			gds->Year = gds->tokenp[0].value;
			gds->tokenp += 1;
			return 1;
		}

		if(gds->tokenp[0].value > 10000) {
			/* "20040301" */
			gds->HaveYear++;
			gds->HaveMonth++;
			gds->HaveDay++;
			gds->Day= (gds->tokenp[0].value)%100;
			gds->Month= (gds->tokenp[0].value/100)%100;
			gds->Year = gds->tokenp[0].value/10000;
			gds->tokenp += 1;
			return 1;
		}

		if (gds->tokenp[0].value < 24) {
			gds->HaveTime++;
			gds->Hour = gds->tokenp[0].value;
			gds->Minutes = 0;
			gds->Seconds = 0;
			gds->tokenp += 1;
			return 1;
		}

		if ((gds->tokenp[0].value / 100 < 24)
		    && (gds->tokenp[0].value % 100 < 60)) {
			/* "513" is same as "5:13" */
			gds->Hour = gds->tokenp[0].value / 100;
			gds->Minutes = gds->tokenp[0].value % 100;
			gds->Seconds = 0;
			gds->tokenp += 1;
			return 1;
		}
	}

	return 0;
}

/*
 * A dictionary of time words.
 */
static struct LEXICON {
	size_t		abbrev;
	const char	*name;
	int		type;
	time_t		value;
} const TimeWords[] = {
	/* am/pm */
	{ 0, "am",		tAMPM,	tAM },
	{ 0, "pm",		tAMPM,	tPM },

	/* Month names. */
	{ 3, "january",		tMONTH,  1 },
	{ 3, "february",	tMONTH,  2 },
	{ 3, "march",		tMONTH,  3 },
	{ 3, "april",		tMONTH,  4 },
	{ 3, "may",		tMONTH,  5 },
	{ 3, "june",		tMONTH,  6 },
	{ 3, "july",		tMONTH,  7 },
	{ 3, "august",		tMONTH,  8 },
	{ 3, "september",	tMONTH,  9 },
	{ 3, "october",		tMONTH, 10 },
	{ 3, "november",	tMONTH, 11 },
	{ 3, "december",	tMONTH, 12 },

	/* Days of the week. */
	{ 2, "sunday",		tDAY, 0 },
	{ 3, "monday",		tDAY, 1 },
	{ 2, "tuesday",		tDAY, 2 },
	{ 3, "wednesday",	tDAY, 3 },
	{ 2, "thursday",	tDAY, 4 },
	{ 2, "friday",		tDAY, 5 },
	{ 2, "saturday",	tDAY, 6 },

	/* Timezones: Offsets are in seconds. */
	{ 0, "gmt",  tZONE,     0*HOUR }, /* Greenwich Mean */
	{ 0, "ut",   tZONE,     0*HOUR }, /* Universal (Coordinated) */
	{ 0, "utc",  tZONE,     0*HOUR },
	{ 0, "wet",  tZONE,     0*HOUR }, /* Western European */
	{ 0, "bst",  tDAYZONE,  0*HOUR }, /* British Summer */
	{ 0, "wat",  tZONE,     1*HOUR }, /* West Africa */
	{ 0, "at",   tZONE,     2*HOUR }, /* Azores */
	/* { 0, "bst", tZONE, 3*HOUR }, */ /* Brazil Standard: Conflict */
	/* { 0, "gst", tZONE, 3*HOUR }, */ /* Greenland Standard: Conflict*/
	{ 0, "nft",  tZONE,     3*HOUR+30*MINUTE }, /* Newfoundland */
	{ 0, "nst",  tZONE,     3*HOUR+30*MINUTE }, /* Newfoundland Standard */
	{ 0, "ndt",  tDAYZONE,  3*HOUR+30*MINUTE }, /* Newfoundland Daylight */
	{ 0, "ast",  tZONE,     4*HOUR }, /* Atlantic Standard */
	{ 0, "adt",  tDAYZONE,  4*HOUR }, /* Atlantic Daylight */
	{ 0, "est",  tZONE,     5*HOUR }, /* Eastern Standard */
	{ 0, "edt",  tDAYZONE,  5*HOUR }, /* Eastern Daylight */
	{ 0, "cst",  tZONE,     6*HOUR }, /* Central Standard */
	{ 0, "cdt",  tDAYZONE,  6*HOUR }, /* Central Daylight */
	{ 0, "mst",  tZONE,     7*HOUR }, /* Mountain Standard */
	{ 0, "mdt",  tDAYZONE,  7*HOUR }, /* Mountain Daylight */
	{ 0, "pst",  tZONE,     8*HOUR }, /* Pacific Standard */
	{ 0, "pdt",  tDAYZONE,  8*HOUR }, /* Pacific Daylight */
	{ 0, "yst",  tZONE,     9*HOUR }, /* Yukon Standard */
	{ 0, "ydt",  tDAYZONE,  9*HOUR }, /* Yukon Daylight */
	{ 0, "hst",  tZONE,     10*HOUR }, /* Hawaii Standard */
	{ 0, "hdt",  tDAYZONE,  10*HOUR }, /* Hawaii Daylight */
	{ 0, "cat",  tZONE,     10*HOUR }, /* Central Alaska */
	{ 0, "ahst", tZONE,     10*HOUR }, /* Alaska-Hawaii Standard */
	{ 0, "nt",   tZONE,     11*HOUR }, /* Nome */
	{ 0, "idlw", tZONE,     12*HOUR }, /* Intl Date Line West */
	{ 0, "cet",  tZONE,     -1*HOUR }, /* Central European */
	{ 0, "met",  tZONE,     -1*HOUR }, /* Middle European */
	{ 0, "mewt", tZONE,     -1*HOUR }, /* Middle European Winter */
	{ 0, "mest", tDAYZONE,  -1*HOUR }, /* Middle European Summer */
	{ 0, "swt",  tZONE,     -1*HOUR }, /* Swedish Winter */
	{ 0, "sst",  tDAYZONE,  -1*HOUR }, /* Swedish Summer */
	{ 0, "fwt",  tZONE,     -1*HOUR }, /* French Winter */
	{ 0, "fst",  tDAYZONE,  -1*HOUR }, /* French Summer */
	{ 0, "eet",  tZONE,     -2*HOUR }, /* Eastern Eur, USSR Zone 1 */
	{ 0, "bt",   tZONE,     -3*HOUR }, /* Baghdad, USSR Zone 2 */
	{ 0, "it",   tZONE,     -3*HOUR-30*MINUTE },/* Iran */
	{ 0, "zp4",  tZONE,     -4*HOUR }, /* USSR Zone 3 */
	{ 0, "zp5",  tZONE,     -5*HOUR }, /* USSR Zone 4 */
	{ 0, "ist",  tZONE,     -5*HOUR-30*MINUTE },/* Indian Standard */
	{ 0, "zp6",  tZONE,     -6*HOUR }, /* USSR Zone 5 */
	/* { 0, "nst",  tZONE, -6.5*HOUR }, */ /* North Sumatra: Conflict */
	/* { 0, "sst", tZONE, -7*HOUR }, */ /* So Sumatra, USSR 6: Conflict */
	{ 0, "wast", tZONE,     -7*HOUR }, /* West Australian Standard */
	{ 0, "wadt", tDAYZONE,  -7*HOUR }, /* West Australian Daylight */
	{ 0, "jt",   tZONE,     -7*HOUR-30*MINUTE },/* Java (3pm in Cronusland!)*/
	{ 0, "cct",  tZONE,     -8*HOUR }, /* China Coast, USSR Zone 7 */
	{ 0, "jst",  tZONE,     -9*HOUR }, /* Japan Std, USSR Zone 8 */
	{ 0, "cast", tZONE,     -9*HOUR-30*MINUTE },/* Ctrl Australian Std */
	{ 0, "cadt", tDAYZONE,  -9*HOUR-30*MINUTE },/* Ctrl Australian Daylt */
	{ 0, "east", tZONE,     -10*HOUR }, /* Eastern Australian Std */
	{ 0, "eadt", tDAYZONE,  -10*HOUR }, /* Eastern Australian Daylt */
	{ 0, "gst",  tZONE,     -10*HOUR }, /* Guam Std, USSR Zone 9 */
	{ 0, "nzt",  tZONE,     -12*HOUR }, /* New Zealand */
	{ 0, "nzst", tZONE,     -12*HOUR }, /* New Zealand Standard */
	{ 0, "nzdt", tDAYZONE,  -12*HOUR }, /* New Zealand Daylight */
	{ 0, "idle", tZONE,     -12*HOUR }, /* Intl Date Line East */

	{ 0, "dst",  tDST,		0 },

	/* Time units. */
	{ 4, "years",		tMONTH_UNIT,	12 },
	{ 5, "months",		tMONTH_UNIT,	1 },
	{ 9, "fortnights",	tSEC_UNIT,	14 * DAY },
	{ 4, "weeks",		tSEC_UNIT,	7 * DAY },
	{ 3, "days",		tSEC_UNIT,	DAY },
	{ 4, "hours",		tSEC_UNIT,	HOUR },
	{ 3, "minutes",		tSEC_UNIT,	MINUTE },
	{ 3, "seconds",		tSEC_UNIT,	1 },

	/* Relative-time words. */
	{ 0, "tomorrow",	tSEC_UNIT,	DAY },
	{ 0, "yesterday",	tSEC_UNIT,	-DAY },
	{ 0, "today",		tSEC_UNIT,	0 },
	{ 0, "now",		tSEC_UNIT,	0 },
	{ 0, "last",		tUNUMBER,	-1 },
	{ 0, "this",		tSEC_UNIT,	0 },
	{ 0, "next",		tUNUMBER,	2 },
	{ 0, "first",		tUNUMBER,	1 },
	{ 0, "1st",		tUNUMBER,	1 },
/*	{ 0, "second",		tUNUMBER,	2 }, */
	{ 0, "2nd",		tUNUMBER,	2 },
	{ 0, "third",		tUNUMBER,	3 },
	{ 0, "3rd",		tUNUMBER,	3 },
	{ 0, "fourth",		tUNUMBER,	4 },
	{ 0, "4th",		tUNUMBER,	4 },
	{ 0, "fifth",		tUNUMBER,	5 },
	{ 0, "5th",		tUNUMBER,	5 },
	{ 0, "sixth",		tUNUMBER,	6 },
	{ 0, "seventh",		tUNUMBER,	7 },
	{ 0, "eighth",		tUNUMBER,	8 },
	{ 0, "ninth",		tUNUMBER,	9 },
	{ 0, "tenth",		tUNUMBER,	10 },
	{ 0, "eleventh",	tUNUMBER,	11 },
	{ 0, "twelfth",		tUNUMBER,	12 },
	{ 0, "ago",		tAGO,		1 },

	/* Military timezones. */
	{ 0, "a",	tZONE,	1*HOUR },
	{ 0, "b",	tZONE,	2*HOUR },
	{ 0, "c",	tZONE,	3*HOUR },
	{ 0, "d",	tZONE,	4*HOUR },
	{ 0, "e",	tZONE,	5*HOUR },
	{ 0, "f",	tZONE,	6*HOUR },
	{ 0, "g",	tZONE,	7*HOUR },
	{ 0, "h",	tZONE,	8*HOUR },
	{ 0, "i",	tZONE,	9*HOUR },
	{ 0, "k",	tZONE,	10*HOUR },
	{ 0, "l",	tZONE,	11*HOUR },
	{ 0, "m",	tZONE,	12*HOUR },
	{ 0, "n",	tZONE,	-1*HOUR },
	{ 0, "o",	tZONE,	-2*HOUR },
	{ 0, "p",	tZONE,	-3*HOUR },
	{ 0, "q",	tZONE,	-4*HOUR },
	{ 0, "r",	tZONE,	-5*HOUR },
	{ 0, "s",	tZONE,	-6*HOUR },
	{ 0, "t",	tZONE,	-7*HOUR },
	{ 0, "u",	tZONE,	-8*HOUR },
	{ 0, "v",	tZONE,	-9*HOUR },
	{ 0, "w",	tZONE,	-10*HOUR },
	{ 0, "x",	tZONE,	-11*HOUR },
	{ 0, "y",	tZONE,	-12*HOUR },
	{ 0, "z",	tZONE,	0*HOUR },

	/* End of table. */
	{ 0, NULL,	0,	0 }
};

/*
 * Year is either:
 *  = A number from 0 to 99, which means a year from 1970 to 2069, or
 *  = The actual year (>=100).
 */
static time_t
Convert(time_t Month, time_t Day, time_t Year,
	time_t Hours, time_t Minutes, time_t Seconds,
	time_t Timezone, enum DSTMODE DSTmode)
{
	signed char DaysInMonth[12] = {
		31, 0, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
	};
	time_t	Julian;
	int	i;

	if (Year < 69)
		Year += 2000;
	else if (Year < 100)
		Year += 1900;
	DaysInMonth[1] = Year % 4 == 0 && (Year % 100 != 0 || Year % 400 == 0)
	    ? 29 : 28;
	/* Checking for 2038 bogusly assumes that time_t is 32 bits.  But
	   I'm too lazy to try to check for time_t overflow in another way.  */
	if (Year < EPOCH || Year > 2038
	    || Month < 1 || Month > 12
	    /* Lint fluff:  "conversion from long may lose accuracy" */
	    || Day < 1 || Day > DaysInMonth[(int)--Month]
	    || Hours < 0 || Hours > 23
	    || Minutes < 0 || Minutes > 59
	    || Seconds < 0 || Seconds > 59)
		return -1;

	Julian = Day - 1;
	for (i = 0; i < Month; i++)
		Julian += DaysInMonth[i];
	for (i = EPOCH; i < Year; i++)
		Julian += 365 + (i % 4 == 0);
	Julian *= DAY;
	Julian += Timezone;
	Julian += Hours * HOUR + Minutes * MINUTE + Seconds;
	if (DSTmode == DSTon
	    || (DSTmode == DSTmaybe && localtime(&Julian)->tm_isdst))
		Julian -= HOUR;
	return Julian;
}


static time_t
DSTcorrect(time_t Start, time_t Future)
{
	time_t	StartDay;
	time_t	FutureDay;

	StartDay = (localtime(&Start)->tm_hour + 1) % 24;
	FutureDay = (localtime(&Future)->tm_hour + 1) % 24;
	return (Future - Start) + (StartDay - FutureDay) * HOUR;
}


static time_t
RelativeDate(time_t Start, time_t zone, int dstmode,
    time_t DayOrdinal, time_t DayNumber)
{
	struct tm	*tm;
	time_t	t, now;

	t = Start - zone;
	tm = gmtime(&t);
	now = Start;
	now += DAY * ((DayNumber - tm->tm_wday + 7) % 7);
	now += 7 * DAY * (DayOrdinal <= 0 ? DayOrdinal : DayOrdinal - 1);
	if (dstmode == DSTmaybe)
		return DSTcorrect(Start, now);
	return now - Start;
}


static time_t
RelativeMonth(time_t Start, time_t Timezone, time_t RelMonth)
{
	struct tm	*tm;
	time_t	Month;
	time_t	Year;

	if (RelMonth == 0)
		return 0;
	tm = localtime(&Start);
	Month = 12 * (tm->tm_year + 1900) + tm->tm_mon + RelMonth;
	Year = Month / 12;
	Month = Month % 12 + 1;
	return DSTcorrect(Start,
	    Convert(Month, (time_t)tm->tm_mday, Year,
		(time_t)tm->tm_hour, (time_t)tm->tm_min, (time_t)tm->tm_sec,
		Timezone, DSTmaybe));
}

/*
 * Tokenizer.
 */
static int
nexttoken(const char **in, time_t *value)
{
	char	c;
	char	buff[64];

	for ( ; ; ) {
		while (isspace((unsigned char)**in))
			++*in;

		/* Skip parenthesized comments. */
		if (**in == '(') {
			int Count = 0;
			do {
				c = *(*in)++;
				if (c == '\0')
					return c;
				if (c == '(')
					Count++;
				else if (c == ')')
					Count--;
			} while (Count > 0);
			continue;
		}

		/* Try the next token in the word table first. */
		/* This allows us to match "2nd", for example. */
		{
			const char *src = *in;
			const struct LEXICON *tp;
			unsigned i = 0;

			/* Force to lowercase and strip '.' characters. */
			while (*src != '\0'
			    && (isalnum((unsigned char)*src) || *src == '.')
			    && i < sizeof(buff)-1) {
				if (*src != '.') {
					if (isupper((unsigned char)*src))
						buff[i++] = tolower((unsigned char)*src);
					else
						buff[i++] = *src;
				}
				src++;
			}
			buff[i] = '\0';

			/*
			 * Find the first match.  If the word can be
			 * abbreviated, make sure we match at least
			 * the minimum abbreviation.
			 */
			for (tp = TimeWords; tp->name; tp++) {
				size_t abbrev = tp->abbrev;
				if (abbrev == 0)
					abbrev = strlen(tp->name);
				if (strlen(buff) >= abbrev
				    && strncmp(tp->name, buff, strlen(buff))
				    	== 0) {
					/* Skip over token. */
					*in = src;
					/* Return the match. */
					*value = tp->value;
					return tp->type;
				}
			}
		}

		/*
		 * Not in the word table, maybe it's a number.  Note:
		 * Because '-' and '+' have other special meanings, I
		 * don't deal with signed numbers here.
		 */
		if (isdigit((unsigned char)(c = **in))) {
			for (*value = 0; isdigit((unsigned char)(c = *(*in)++)); )
				*value = 10 * *value + c - '0';
			(*in)--;
			return (tUNUMBER);
		}

		return *(*in)++;
	}
}

#define	TM_YEAR_ORIGIN 1900

/* Yield A - B, measured in seconds.  */
static long
difftm (struct tm *a, struct tm *b)
{
	int ay = a->tm_year + (TM_YEAR_ORIGIN - 1);
	int by = b->tm_year + (TM_YEAR_ORIGIN - 1);
	int days = (
		/* difference in day of year */
		a->tm_yday - b->tm_yday
		/* + intervening leap days */
		+  ((ay >> 2) - (by >> 2))
		-  (ay/100 - by/100)
		+  ((ay/100 >> 2) - (by/100 >> 2))
		/* + difference in years * 365 */
		+  (long)(ay-by) * 365
		);
	return (days * DAY + (a->tm_hour - b->tm_hour) * HOUR
	    + (a->tm_min - b->tm_min) * MINUTE
	    + (a->tm_sec - b->tm_sec));
}

/*
 *
 * The public function.
 *
 * TODO: tokens[] array should be dynamically sized.
 */
time_t
__archive_get_date(time_t now, const char *p)
{
	struct token	tokens[256];
	struct gdstate	_gds;
	struct token	*lasttoken;
	struct gdstate	*gds;
	struct tm	local, *tm;
	struct tm	gmt, *gmt_ptr;
	time_t		Start;
	time_t		tod;
	long		tzone;

	/* Clear out the parsed token array. */
	memset(tokens, 0, sizeof(tokens));
	/* Initialize the parser state. */
	memset(&_gds, 0, sizeof(_gds));
	gds = &_gds;

	/* Look up the current time. */
	memset(&local, 0, sizeof(local));
	tm = localtime (&now);
	if (tm == NULL)
		return -1;
	local = *tm;

	/* Look up UTC if we can and use that to determine the current
	 * timezone offset. */
	memset(&gmt, 0, sizeof(gmt));
	gmt_ptr = gmtime (&now);
	if (gmt_ptr != NULL) {
		/* Copy, in case localtime and gmtime use the same buffer. */
		gmt = *gmt_ptr;
	}
	if (gmt_ptr != NULL)
		tzone = difftm (&gmt, &local);
	else
		/* This system doesn't understand timezones; fake it. */
		tzone = 0;
	if(local.tm_isdst)
		tzone += HOUR;

	/* Tokenize the input string. */
	lasttoken = tokens;
	while ((lasttoken->token = nexttoken(&p, &lasttoken->value)) != 0) {
		++lasttoken;
		if (lasttoken > tokens + 255)
			return -1;
	}
	gds->tokenp = tokens;

	/* Match phrases until we run out of input tokens. */
	while (gds->tokenp < lasttoken) {
		if (!phrase(gds))
			return -1;
	}

	/* Use current local timezone if none was specified. */
	if (!gds->HaveZone) {
		gds->Timezone = tzone;
		gds->DSTmode = DSTmaybe;
	}

	/* If a timezone was specified, use that for generating the default
	 * time components instead of the local timezone. */
	if (gds->HaveZone && gmt_ptr != NULL) {
		now -= gds->Timezone;
		gmt_ptr = gmtime (&now);
		if (gmt_ptr != NULL)
			local = *gmt_ptr;
		now += gds->Timezone;
	}

	if (!gds->HaveYear)
		gds->Year = local.tm_year + 1900;
	if (!gds->HaveMonth)
		gds->Month = local.tm_mon + 1;
	if (!gds->HaveDay)
		gds->Day = local.tm_mday;
	/* Note: No default for hour/min/sec; a specifier that just
	 * gives date always refers to 00:00 on that date. */

	/* If we saw more than one time, timezone, weekday, year, month,
	 * or day, then give up. */
	if (gds->HaveTime > 1 || gds->HaveZone > 1 || gds->HaveWeekDay > 1
	    || gds->HaveYear > 1 || gds->HaveMonth > 1 || gds->HaveDay > 1)
		return -1;

	/* Compute an absolute time based on whatever absolute information
	 * we collected. */
	if (gds->HaveYear || gds->HaveMonth || gds->HaveDay
	    || gds->HaveTime || gds->HaveWeekDay) {
		Start = Convert(gds->Month, gds->Day, gds->Year,
		    gds->Hour, gds->Minutes, gds->Seconds,
		    gds->Timezone, gds->DSTmode);
		if (Start < 0)
			return -1;
	} else {
		Start = now;
		if (!gds->HaveRel)
			Start -= local.tm_hour * HOUR + local.tm_min * MINUTE
			    + local.tm_sec;
	}

	/* Add the relative offset. */
	Start += gds->RelSeconds;
	Start += RelativeMonth(Start, gds->Timezone, gds->RelMonth);

	/* Adjust for day-of-week offsets. */
	if (gds->HaveWeekDay
	    && !(gds->HaveYear || gds->HaveMonth || gds->HaveDay)) {
		tod = RelativeDate(Start, gds->Timezone,
		    gds->DSTmode, gds->DayOrdinal, gds->DayNumber);
		Start += tod;
	}

	/* -1 is an error indicator, so return 0 instead of -1 if
	 * that's the actual time. */
	return Start == -1 ? 0 : Start;
}


#if	defined(TEST)

/* ARGSUSED */
int
main(int argc, char **argv)
{
    time_t	d;
    time_t	now = time(NULL);

    while (*++argv != NULL) {
	    (void)printf("Input: %s\n", *argv);
	    d = get_date(now, *argv);
	    if (d == -1)
		    (void)printf("Bad format - couldn't convert.\n");
	    else
		    (void)printf("Output: %s\n", ctime(&d));
    }
    exit(0);
    /* NOTREACHED */
}
#endif	/* defined(TEST) */
