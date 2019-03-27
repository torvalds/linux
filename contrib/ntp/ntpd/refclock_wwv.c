/*
 * refclock_wwv - clock driver for NIST WWV/H time/frequency station
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#if defined(REFCLOCK) && defined(CLOCK_WWV)

#include "ntpd.h"
#include "ntp_io.h"
#include "ntp_refclock.h"
#include "ntp_calendar.h"
#include "ntp_stdlib.h"
#include "audio.h"

#include <stdio.h>
#include <ctype.h>
#include <math.h>
#ifdef HAVE_SYS_IOCTL_H
# include <sys/ioctl.h>
#endif /* HAVE_SYS_IOCTL_H */

#define ICOM 1

#ifdef ICOM
#include "icom.h"
#endif /* ICOM */

/*
 * Audio WWV/H demodulator/decoder
 *
 * This driver synchronizes the computer time using data encoded in
 * radio transmissions from NIST time/frequency stations WWV in Boulder,
 * CO, and WWVH in Kauai, HI. Transmissions are made continuously on
 * 2.5, 5, 10 and 15 MHz from WWV and WWVH, and 20 MHz from WWV. An
 * ordinary AM shortwave receiver can be tuned manually to one of these
 * frequencies or, in the case of ICOM receivers, the receiver can be
 * tuned automatically using this program as propagation conditions
 * change throughout the weasons, both day and night.
 *
 * The driver requires an audio codec or sound card with sampling rate 8
 * kHz and mu-law companding. This is the same standard as used by the
 * telephone industry and is supported by most hardware and operating
 * systems, including Solaris, SunOS, FreeBSD, NetBSD and Linux. In this
 * implementation, only one audio driver and codec can be supported on a
 * single machine.
 *
 * The demodulation and decoding algorithms used in this driver are
 * based on those developed for the TAPR DSP93 development board and the
 * TI 320C25 digital signal processor described in: Mills, D.L. A
 * precision radio clock for WWV transmissions. Electrical Engineering
 * Report 97-8-1, University of Delaware, August 1997, 25 pp., available
 * from www.eecis.udel.edu/~mills/reports.html. The algorithms described
 * in this report have been modified somewhat to improve performance
 * under weak signal conditions and to provide an automatic station
 * identification feature.
 *
 * The ICOM code is normally compiled in the driver. It isn't used,
 * unless the mode keyword on the server configuration command specifies
 * a nonzero ICOM ID select code. The C-IV trace is turned on if the
 * debug level is greater than one.
 *
 * Fudge factors
 *
 * Fudge flag4 causes the debugging output described above to be
 * recorded in the clockstats file. Fudge flag2 selects the audio input
 * port, where 0 is the mike port (default) and 1 is the line-in port.
 * It does not seem useful to select the compact disc player port. Fudge
 * flag3 enables audio monitoring of the input signal. For this purpose,
 * the monitor gain is set to a default value.
 *
 * CEVNT_BADTIME	invalid date or time
 * CEVNT_PROP		propagation failure - no stations heard
 * CEVNT_TIMEOUT	timeout (see newgame() below)
 */
/*
 * General definitions. These ordinarily do not need to be changed.
 */
#define	DEVICE_AUDIO	"/dev/audio" /* audio device name */
#define	AUDIO_BUFSIZ	320	/* audio buffer size (50 ms) */
#define	PRECISION	(-10)	/* precision assumed (about 1 ms) */
#define	DESCRIPTION	"WWV/H Audio Demodulator/Decoder" /* WRU */
#define WWV_SEC		8000	/* second epoch (sample rate) (Hz) */
#define WWV_MIN		(WWV_SEC * 60) /* minute epoch */
#define OFFSET		128	/* companded sample offset */
#define SIZE		256	/* decompanding table size */
#define	MAXAMP		6000.	/* max signal level reference */
#define	MAXCLP		100	/* max clips above reference per s */
#define MAXSNR		40.	/* max SNR reference */
#define MAXFREQ		1.5	/* max frequency tolerance (187 PPM) */
#define DATCYC		170	/* data filter cycles */
#define DATSIZ		(DATCYC * MS) /* data filter size */
#define SYNCYC		800	/* minute filter cycles */
#define SYNSIZ		(SYNCYC * MS) /* minute filter size */
#define TCKCYC		5	/* tick filter cycles */
#define TCKSIZ		(TCKCYC * MS) /* tick filter size */
#define NCHAN		5	/* number of radio channels */
#define	AUDIO_PHI	5e-6	/* dispersion growth factor */
#define	TBUF		128	/* max monitor line length */

/*
 * Tunable parameters. The DGAIN parameter can be changed to fit the
 * audio response of the radio at 100 Hz. The WWV/WWVH data subcarrier
 * is transmitted at about 20 percent percent modulation; the matched
 * filter boosts it by a factor of 17 and the receiver response does
 * what it does. The compromise value works for ICOM radios. If the
 * radio is not tunable, the DCHAN parameter can be changed to fit the
 * expected best propagation frequency: higher if further from the
 * transmitter, lower if nearer. The compromise value works for the US
 * right coast.
 */
#define DCHAN		3	/* default radio channel (15 Mhz) */
#define DGAIN		5.	/* subcarrier gain */

/*
 * General purpose status bits (status)
 *
 * SELV and/or SELH are set when WWV or WWVH have been heard and cleared
 * on signal loss. SSYNC is set when the second sync pulse has been
 * acquired and cleared by signal loss. MSYNC is set when the minute
 * sync pulse has been acquired. DSYNC is set when the units digit has
 * has reached the threshold and INSYNC is set when all nine digits have
 * reached the threshold. The MSYNC, DSYNC and INSYNC bits are cleared
 * only by timeout, upon which the driver starts over from scratch.
 *
 * DGATE is lit if the data bit amplitude or SNR is below thresholds and
 * BGATE is lit if the pulse width amplitude or SNR is below thresolds.
 * LEPSEC is set during the last minute of the leap day. At the end of
 * this minute the driver inserts second 60 in the seconds state machine
 * and the minute sync slips a second.
 */
#define MSYNC		0x0001	/* minute epoch sync */
#define SSYNC		0x0002	/* second epoch sync */
#define DSYNC		0x0004	/* minute units sync */
#define INSYNC		0x0008	/* clock synchronized */
#define FGATE		0x0010	/* frequency gate */
#define DGATE		0x0020	/* data pulse amplitude error */
#define BGATE		0x0040	/* data pulse width error */
#define	METRIC		0x0080	/* one or more stations heard */
#define LEPSEC		0x1000	/* leap minute */

/*
 * Station scoreboard bits
 *
 * These are used to establish the signal quality for each of the five
 * frequencies and two stations.
 */
#define SELV		0x0100	/* WWV station select */
#define SELH		0x0200	/* WWVH station select */

/*
 * Alarm status bits (alarm)
 *
 * These bits indicate various alarm conditions, which are decoded to
 * form the quality character included in the timecode.
 */
#define CMPERR		0x1	/* digit or misc bit compare error */
#define LOWERR		0x2	/* low bit or digit amplitude or SNR */
#define NINERR		0x4	/* less than nine digits in minute */
#define SYNERR		0x8	/* not tracking second sync */

/*
 * Watchcat timeouts (watch)
 *
 * If these timeouts expire, the status bits are mashed to zero and the
 * driver starts from scratch. Suitably more refined procedures may be
 * developed in future. All these are in minutes.
 */
#define ACQSN		6	/* station acquisition timeout */
#define DATA		15	/* unit minutes timeout */
#define SYNCH		40	/* station sync timeout */
#define PANIC		(2 * 1440) /* panic timeout */

/*
 * Thresholds. These establish the minimum signal level, minimum SNR and
 * maximum jitter thresholds which establish the error and false alarm
 * rates of the driver. The values defined here may be on the
 * adventurous side in the interest of the highest sensitivity.
 */
#define MTHR		13.	/* minute sync gate (percent) */
#define TTHR		50.	/* minute sync threshold (percent) */
#define AWND		20	/* minute sync jitter threshold (ms) */
#define ATHR		2500.	/* QRZ minute sync threshold */
#define ASNR		20.	/* QRZ minute sync SNR threshold (dB) */
#define QTHR		2500.	/* QSY minute sync threshold */
#define QSNR		20.	/* QSY minute sync SNR threshold (dB) */
#define STHR		2500.	/* second sync threshold */
#define	SSNR		15.	/* second sync SNR threshold (dB) */
#define SCMP		10 	/* second sync compare threshold */
#define DTHR		1000.	/* bit threshold */
#define DSNR		10.	/* bit SNR threshold (dB) */
#define AMIN		3	/* min bit count */
#define AMAX		6	/* max bit count */
#define BTHR		1000.	/* digit threshold */
#define BSNR		3.	/* digit likelihood threshold (dB) */
#define BCMP		3	/* digit compare threshold */
#define	MAXERR		40	/* maximum error alarm */

/*
 * Tone frequency definitions. The increments are for 4.5-deg sine
 * table.
 */
#define MS		(WWV_SEC / 1000) /* samples per millisecond */
#define IN100		((100 * 80) / WWV_SEC) /* 100 Hz increment */
#define IN1000		((1000 * 80) / WWV_SEC) /* 1000 Hz increment */
#define IN1200		((1200 * 80) / WWV_SEC) /* 1200 Hz increment */

/*
 * Acquisition and tracking time constants
 */
#define MINAVG		8	/* min averaging time */
#define MAXAVG		1024	/* max averaging time */
#define FCONST		3	/* frequency time constant */
#define TCONST		16	/* data bit/digit time constant */

/*
 * Miscellaneous status bits (misc)
 *
 * These bits correspond to designated bits in the WWV/H timecode. The
 * bit probabilities are exponentially averaged over several minutes and
 * processed by a integrator and threshold.
 */
#define DUT1		0x01	/* 56 DUT .1 */
#define DUT2		0x02	/* 57 DUT .2 */
#define DUT4		0x04	/* 58 DUT .4 */
#define DUTS		0x08	/* 50 DUT sign */
#define DST1		0x10	/* 55 DST1 leap warning */
#define DST2		0x20	/* 2 DST2 DST1 delayed one day */
#define SECWAR		0x40	/* 3 leap second warning */

/*
 * The on-time synchronization point is the positive-going zero crossing
 * of the first cycle of the 5-ms second pulse. The IIR baseband filter
 * phase delay is 0.91 ms, while the receiver delay is approximately 4.7
 * ms at 1000 Hz. The fudge value -0.45 ms due to the codec and other
 * causes was determined by calibrating to a PPS signal from a GPS
 * receiver. The additional propagation delay specific to each receiver
 * location can be  programmed in the fudge time1 and time2 values for
 * WWV and WWVH, respectively.
 *
 * The resulting offsets with a 2.4-GHz P4 running FreeBSD 6.1 are
 * generally within .02 ms short-term with .02 ms jitter. The long-term
 * offsets vary up to 0.3 ms due to ionosperhic layer height variations.
 * The processor load due to the driver is 5.8 percent.
 */
#define PDELAY	((.91 + 4.7 - 0.45) / 1000) /* system delay (s) */

/*
 * Table of sine values at 4.5-degree increments. This is used by the
 * synchronous matched filter demodulators.
 */
double sintab[] = {
 0.000000e+00,  7.845910e-02,  1.564345e-01,  2.334454e-01, /* 0-3 */
 3.090170e-01,  3.826834e-01,  4.539905e-01,  5.224986e-01, /* 4-7 */
 5.877853e-01,  6.494480e-01,  7.071068e-01,  7.604060e-01, /* 8-11 */
 8.090170e-01,  8.526402e-01,  8.910065e-01,  9.238795e-01, /* 12-15 */
 9.510565e-01,  9.723699e-01,  9.876883e-01,  9.969173e-01, /* 16-19 */
 1.000000e+00,  9.969173e-01,  9.876883e-01,  9.723699e-01, /* 20-23 */
 9.510565e-01,  9.238795e-01,  8.910065e-01,  8.526402e-01, /* 24-27 */
 8.090170e-01,  7.604060e-01,  7.071068e-01,  6.494480e-01, /* 28-31 */
 5.877853e-01,  5.224986e-01,  4.539905e-01,  3.826834e-01, /* 32-35 */
 3.090170e-01,  2.334454e-01,  1.564345e-01,  7.845910e-02, /* 36-39 */
-0.000000e+00, -7.845910e-02, -1.564345e-01, -2.334454e-01, /* 40-43 */
-3.090170e-01, -3.826834e-01, -4.539905e-01, -5.224986e-01, /* 44-47 */
-5.877853e-01, -6.494480e-01, -7.071068e-01, -7.604060e-01, /* 48-51 */
-8.090170e-01, -8.526402e-01, -8.910065e-01, -9.238795e-01, /* 52-55 */
-9.510565e-01, -9.723699e-01, -9.876883e-01, -9.969173e-01, /* 56-59 */
-1.000000e+00, -9.969173e-01, -9.876883e-01, -9.723699e-01, /* 60-63 */
-9.510565e-01, -9.238795e-01, -8.910065e-01, -8.526402e-01, /* 64-67 */
-8.090170e-01, -7.604060e-01, -7.071068e-01, -6.494480e-01, /* 68-71 */
-5.877853e-01, -5.224986e-01, -4.539905e-01, -3.826834e-01, /* 72-75 */
-3.090170e-01, -2.334454e-01, -1.564345e-01, -7.845910e-02, /* 76-79 */
 0.000000e+00};						    /* 80 */

/*
 * Decoder operations at the end of each second are driven by a state
 * machine. The transition matrix consists of a dispatch table indexed
 * by second number. Each entry in the table contains a case switch
 * number and argument.
 */
struct progx {
	int sw;			/* case switch number */
	int arg;		/* argument */
};

/*
 * Case switch numbers
 */
#define IDLE		0	/* no operation */
#define COEF		1	/* BCD bit */
#define COEF1		2	/* BCD bit for minute unit */
#define COEF2		3	/* BCD bit not used */
#define DECIM9		4	/* BCD digit 0-9 */
#define DECIM6		5	/* BCD digit 0-6 */
#define DECIM3		6	/* BCD digit 0-3 */
#define DECIM2		7	/* BCD digit 0-2 */
#define MSCBIT		8	/* miscellaneous bit */
#define MSC20		9	/* miscellaneous bit */		
#define MSC21		10	/* QSY probe channel */		
#define MIN1		11	/* latch time */		
#define MIN2		12	/* leap second */
#define SYNC2		13	/* latch minute sync pulse */		
#define SYNC3		14	/* latch data pulse */		

/*
 * Offsets in decoding matrix
 */
#define MN		0	/* minute digits (2) */
#define HR		2	/* hour digits (2) */
#define DA		4	/* day digits (3) */
#define YR		7	/* year digits (2) */

struct progx progx[] = {
	{SYNC2,	0},		/* 0 latch minute sync pulse */
	{SYNC3,	0},		/* 1 latch data pulse */
	{MSCBIT, DST2},		/* 2 dst2 */
	{MSCBIT, SECWAR},	/* 3 lw */
	{COEF,	0},		/* 4 1 year units */
	{COEF,	1},		/* 5 2 */
	{COEF,	2},		/* 6 4 */
	{COEF,	3},		/* 7 8 */
	{DECIM9, YR},		/* 8 */
	{IDLE,	0},		/* 9 p1 */
	{COEF1,	0},		/* 10 1 minute units */
	{COEF1,	1},		/* 11 2 */
	{COEF1,	2},		/* 12 4 */
	{COEF1,	3},		/* 13 8 */
	{DECIM9, MN},		/* 14 */
	{COEF,	0},		/* 15 10 minute tens */
	{COEF,	1},		/* 16 20 */
	{COEF,	2},		/* 17 40 */
	{COEF2,	3},		/* 18 80 (not used) */
	{DECIM6, MN + 1},	/* 19 p2 */
	{COEF,	0},		/* 20 1 hour units */
	{COEF,	1},		/* 21 2 */
	{COEF,	2},		/* 22 4 */
	{COEF,	3},		/* 23 8 */
	{DECIM9, HR},		/* 24 */
	{COEF,	0},		/* 25 10 hour tens */
	{COEF,	1},		/* 26 20 */
	{COEF2,	2},		/* 27 40 (not used) */
	{COEF2,	3},		/* 28 80 (not used) */
	{DECIM2, HR + 1},	/* 29 p3 */
	{COEF,	0},		/* 30 1 day units */
	{COEF,	1},		/* 31 2 */
	{COEF,	2},		/* 32 4 */
	{COEF,	3},		/* 33 8 */
	{DECIM9, DA},		/* 34 */
	{COEF,	0},		/* 35 10 day tens */
	{COEF,	1},		/* 36 20 */
	{COEF,	2},		/* 37 40 */
	{COEF,	3},		/* 38 80 */
	{DECIM9, DA + 1},	/* 39 p4 */
	{COEF,	0},		/* 40 100 day hundreds */
	{COEF,	1},		/* 41 200 */
	{COEF2,	2},		/* 42 400 (not used) */
	{COEF2,	3},		/* 43 800 (not used) */
	{DECIM3, DA + 2},	/* 44 */
	{IDLE,	0},		/* 45 */
	{IDLE,	0},		/* 46 */
	{IDLE,	0},		/* 47 */
	{IDLE,	0},		/* 48 */
	{IDLE,	0},		/* 49 p5 */
	{MSCBIT, DUTS},		/* 50 dut+- */
	{COEF,	0},		/* 51 10 year tens */
	{COEF,	1},		/* 52 20 */
	{COEF,	2},		/* 53 40 */
	{COEF,	3},		/* 54 80 */
	{MSC20, DST1},		/* 55 dst1 */
	{MSCBIT, DUT1},		/* 56 0.1 dut */
	{MSCBIT, DUT2},		/* 57 0.2 */
	{MSC21, DUT4},		/* 58 0.4 QSY probe channel */
	{MIN1,	0},		/* 59 p6 latch time */
	{MIN2,	0}		/* 60 leap second */
};

/*
 * BCD coefficients for maximum-likelihood digit decode
 */
#define P15	1.		/* max positive number */
#define N15	-1.		/* max negative number */

/*
 * Digits 0-9
 */
#define P9	(P15 / 4)	/* mark (+1) */
#define N9	(N15 / 4)	/* space (-1) */

double bcd9[][4] = {
	{N9, N9, N9, N9}, 	/* 0 */
	{P9, N9, N9, N9}, 	/* 1 */
	{N9, P9, N9, N9}, 	/* 2 */
	{P9, P9, N9, N9}, 	/* 3 */
	{N9, N9, P9, N9}, 	/* 4 */
	{P9, N9, P9, N9}, 	/* 5 */
	{N9, P9, P9, N9}, 	/* 6 */
	{P9, P9, P9, N9}, 	/* 7 */
	{N9, N9, N9, P9}, 	/* 8 */
	{P9, N9, N9, P9}, 	/* 9 */
	{0, 0, 0, 0}		/* backstop */
};

/*
 * Digits 0-6 (minute tens)
 */
#define P6	(P15 / 3)	/* mark (+1) */
#define N6	(N15 / 3)	/* space (-1) */

double bcd6[][4] = {
	{N6, N6, N6, 0}, 	/* 0 */
	{P6, N6, N6, 0}, 	/* 1 */
	{N6, P6, N6, 0}, 	/* 2 */
	{P6, P6, N6, 0}, 	/* 3 */
	{N6, N6, P6, 0}, 	/* 4 */
	{P6, N6, P6, 0}, 	/* 5 */
	{N6, P6, P6, 0}, 	/* 6 */
	{0, 0, 0, 0}		/* backstop */
};

/*
 * Digits 0-3 (day hundreds)
 */
#define P3	(P15 / 2)	/* mark (+1) */
#define N3	(N15 / 2)	/* space (-1) */

double bcd3[][4] = {
	{N3, N3, 0, 0}, 	/* 0 */
	{P3, N3, 0, 0}, 	/* 1 */
	{N3, P3, 0, 0}, 	/* 2 */
	{P3, P3, 0, 0}, 	/* 3 */
	{0, 0, 0, 0}		/* backstop */
};

/*
 * Digits 0-2 (hour tens)
 */
#define P2	(P15 / 2)	/* mark (+1) */
#define N2	(N15 / 2)	/* space (-1) */

double bcd2[][4] = {
	{N2, N2, 0, 0}, 	/* 0 */
	{P2, N2, 0, 0}, 	/* 1 */
	{N2, P2, 0, 0}, 	/* 2 */
	{0, 0, 0, 0}		/* backstop */
};

/*
 * DST decode (DST2 DST1) for prettyprint
 */
char dstcod[] = {
	'S',			/* 00 standard time */
	'I',			/* 01 set clock ahead at 0200 local */
	'O',			/* 10 set clock back at 0200 local */
	'D'			/* 11 daylight time */
};

/*
 * The decoding matrix consists of nine row vectors, one for each digit
 * of the timecode. The digits are stored from least to most significant
 * order. The maximum-likelihood timecode is formed from the digits
 * corresponding to the maximum-likelihood values reading in the
 * opposite order: yy ddd hh:mm.
 */
struct decvec {
	int radix;		/* radix (3, 4, 6, 10) */
	int digit;		/* current clock digit */
	int count;		/* match count */
	double digprb;		/* max digit probability */
	double digsnr;		/* likelihood function (dB) */
	double like[10];	/* likelihood integrator 0-9 */
};

/*
 * The station structure (sp) is used to acquire the minute pulse from
 * WWV and/or WWVH. These stations are distinguished by the frequency
 * used for the second and minute sync pulses, 1000 Hz for WWV and 1200
 * Hz for WWVH. Other than frequency, the format is the same.
 */
struct sync {
	double	epoch;		/* accumulated epoch differences */
	double	maxeng;		/* sync max energy */
	double	noieng;		/* sync noise energy */
	long	pos;		/* max amplitude position */
	long	lastpos;	/* last max position */
	long	mepoch;		/* minute synch epoch */

	double	amp;		/* sync signal */
	double	syneng;		/* sync signal max */
	double	synmax;		/* sync signal max latched at 0 s */
	double	synsnr;		/* sync signal SNR */
	double	metric;		/* signal quality metric */
	int	reach;		/* reachability register */
	int	count;		/* bit counter */
	int	select;		/* select bits */
	char	refid[5];	/* reference identifier */
};

/*
 * The channel structure (cp) is used to mitigate between channels.
 */
struct chan {
	int	gain;		/* audio gain */
	struct sync wwv;	/* wwv station */
	struct sync wwvh;	/* wwvh station */
};

/*
 * WWV unit control structure (up)
 */
struct wwvunit {
	l_fp	timestamp;	/* audio sample timestamp */
	l_fp	tick;		/* audio sample increment */
	double	phase, freq;	/* logical clock phase and frequency */
	double	monitor;	/* audio monitor point */
	double	pdelay;		/* propagation delay (s) */
#ifdef ICOM
	int	fd_icom;	/* ICOM file descriptor */
#endif /* ICOM */
	int	errflg;		/* error flags */
	int	watch;		/* watchcat */

	/*
	 * Audio codec variables
	 */
	double	comp[SIZE];	/* decompanding table */
 	int	port;		/* codec port */
	int	gain;		/* codec gain */
	int	mongain;	/* codec monitor gain */
	int	clipcnt;	/* sample clipped count */

	/*
	 * Variables used to establish basic system timing
	 */
	int	avgint;		/* master time constant */
	int	yepoch;		/* sync epoch */
	int	repoch;		/* buffered sync epoch */
	double	epomax;		/* second sync amplitude */
	double	eposnr;		/* second sync SNR */
	double	irig;		/* data I channel amplitude */
	double	qrig;		/* data Q channel amplitude */
	int	datapt;		/* 100 Hz ramp */
	double	datpha;		/* 100 Hz VFO control */
	int	rphase;		/* second sample counter */
	long	mphase;		/* minute sample counter */

	/*
	 * Variables used to mitigate which channel to use
	 */
	struct chan mitig[NCHAN]; /* channel data */
	struct sync *sptr;	/* station pointer */
	int	dchan;		/* data channel */
	int	schan;		/* probe channel */
	int	achan;		/* active channel */

	/*
	 * Variables used by the clock state machine
	 */
	struct decvec decvec[9]; /* decoding matrix */
	int	rsec;		/* seconds counter */
	int	digcnt;		/* count of digits synchronized */

	/*
	 * Variables used to estimate signal levels and bit/digit
	 * probabilities
	 */
	double	datsig;		/* data signal max */
	double	datsnr;		/* data signal SNR (dB) */

	/*
	 * Variables used to establish status and alarm conditions
	 */
	int	status;		/* status bits */
	int	alarm;		/* alarm flashers */
	int	misc;		/* miscellaneous timecode bits */
	int	errcnt;		/* data bit error counter */
};

/*
 * Function prototypes
 */
static	int	wwv_start	(int, struct peer *);
static	void	wwv_shutdown	(int, struct peer *);
static	void	wwv_receive	(struct recvbuf *);
static	void	wwv_poll	(int, struct peer *);

/*
 * More function prototypes
 */
static	void	wwv_epoch	(struct peer *);
static	void	wwv_rf		(struct peer *, double);
static	void	wwv_endpoc	(struct peer *, int);
static	void	wwv_rsec	(struct peer *, double);
static	void	wwv_qrz		(struct peer *, struct sync *, int);
static	void	wwv_corr4	(struct peer *, struct decvec *,
				    double [], double [][4]);
static	void	wwv_gain	(struct peer *);
static	void	wwv_tsec	(struct peer *);
static	int	timecode	(struct wwvunit *, char *, size_t);
static	double	wwv_snr		(double, double);
static	int	carry		(struct decvec *);
static	int	wwv_newchan	(struct peer *);
static	void	wwv_newgame	(struct peer *);
static	double	wwv_metric	(struct sync *);
static	void	wwv_clock	(struct peer *);
#ifdef ICOM
static	int	wwv_qsy		(struct peer *, int);
#endif /* ICOM */

static double qsy[NCHAN] = {2.5, 5, 10, 15, 20}; /* frequencies (MHz) */

/*
 * Transfer vector
 */
struct	refclock refclock_wwv = {
	wwv_start,		/* start up driver */
	wwv_shutdown,		/* shut down driver */
	wwv_poll,		/* transmit poll message */
	noentry,		/* not used (old wwv_control) */
	noentry,		/* initialize driver (not used) */
	noentry,		/* not used (old wwv_buginfo) */
	NOFLAGS			/* not used */
};


/*
 * wwv_start - open the devices and initialize data for processing
 */
static int
wwv_start(
	int	unit,		/* instance number (used by PCM) */
	struct peer *peer	/* peer structure pointer */
	)
{
	struct refclockproc *pp;
	struct wwvunit *up;
#ifdef ICOM
	int	temp;
#endif /* ICOM */

	/*
	 * Local variables
	 */
	int	fd;		/* file descriptor */
	int	i;		/* index */
	double	step;		/* codec adjustment */

	/*
	 * Open audio device
	 */
	fd = audio_init(DEVICE_AUDIO, AUDIO_BUFSIZ, unit);
	if (fd < 0)
		return (0);
#ifdef DEBUG
	if (debug)
		audio_show();
#endif /* DEBUG */

	/*
	 * Allocate and initialize unit structure
	 */
	up = emalloc_zero(sizeof(*up));
	pp = peer->procptr;
	pp->io.clock_recv = wwv_receive;
	pp->io.srcclock = peer;
	pp->io.datalen = 0;
	pp->io.fd = fd;
	if (!io_addclock(&pp->io)) {
		close(fd);
		free(up);
		return (0);
	}
	pp->unitptr = up;

	/*
	 * Initialize miscellaneous variables
	 */
	peer->precision = PRECISION;
	pp->clockdesc = DESCRIPTION;

	/*
	 * The companded samples are encoded sign-magnitude. The table
	 * contains all the 256 values in the interest of speed.
	 */
	up->comp[0] = up->comp[OFFSET] = 0.;
	up->comp[1] = 1.; up->comp[OFFSET + 1] = -1.;
	up->comp[2] = 3.; up->comp[OFFSET + 2] = -3.;
	step = 2.;
	for (i = 3; i < OFFSET; i++) {
		up->comp[i] = up->comp[i - 1] + step;
		up->comp[OFFSET + i] = -up->comp[i];
		if (i % 16 == 0)
			step *= 2.;
	}
	DTOLFP(1. / WWV_SEC, &up->tick);

	/*
	 * Initialize the decoding matrix with the radix for each digit
	 * position.
	 */
	up->decvec[MN].radix = 10;	/* minutes */
	up->decvec[MN + 1].radix = 6;
	up->decvec[HR].radix = 10;	/* hours */
	up->decvec[HR + 1].radix = 3;
	up->decvec[DA].radix = 10;	/* days */
	up->decvec[DA + 1].radix = 10;
	up->decvec[DA + 2].radix = 4;
	up->decvec[YR].radix = 10;	/* years */
	up->decvec[YR + 1].radix = 10;

#ifdef ICOM
	/*
	 * Initialize autotune if available. Note that the ICOM select
	 * code must be less than 128, so the high order bit can be used
	 * to select the line speed 0 (9600 bps) or 1 (1200 bps). Note
	 * we don't complain if the ICOM device is not there; but, if it
	 * is, the radio better be working.
	 */
	temp = 0;
#ifdef DEBUG
	if (debug > 1)
		temp = P_TRACE;
#endif /* DEBUG */
	if (peer->ttl != 0) {
		if (peer->ttl & 0x80)
			up->fd_icom = icom_init("/dev/icom", B1200,
			    temp);
		else
			up->fd_icom = icom_init("/dev/icom", B9600,
			    temp);
	}
	if (up->fd_icom > 0) {
		if (wwv_qsy(peer, DCHAN) != 0) {
			msyslog(LOG_NOTICE, "icom: radio not found");
			close(up->fd_icom);
			up->fd_icom = 0;
		} else {
			msyslog(LOG_NOTICE, "icom: autotune enabled");
		}
	}
#endif /* ICOM */

	/*
	 * Let the games begin.
	 */
	wwv_newgame(peer);
	return (1);
}


/*
 * wwv_shutdown - shut down the clock
 */
static void
wwv_shutdown(
	int	unit,		/* instance number (not used) */
	struct peer *peer	/* peer structure pointer */
	)
{
	struct refclockproc *pp;
	struct wwvunit *up;

	pp = peer->procptr;
	up = pp->unitptr;
	if (up == NULL)
		return;

	io_closeclock(&pp->io);
#ifdef ICOM
	if (up->fd_icom > 0)
		close(up->fd_icom);
#endif /* ICOM */
	free(up);
}


/*
 * wwv_receive - receive data from the audio device
 *
 * This routine reads input samples and adjusts the logical clock to
 * track the A/D sample clock by dropping or duplicating codec samples.
 * It also controls the A/D signal level with an AGC loop to mimimize
 * quantization noise and avoid overload.
 */
static void
wwv_receive(
	struct recvbuf *rbufp	/* receive buffer structure pointer */
	)
{
	struct peer *peer;
	struct refclockproc *pp;
	struct wwvunit *up;

	/*
	 * Local variables
	 */
	double	sample;		/* codec sample */
	u_char	*dpt;		/* buffer pointer */
	int	bufcnt;		/* buffer counter */
	l_fp	ltemp;

	peer = rbufp->recv_peer;
	pp = peer->procptr;
	up = pp->unitptr;

	/*
	 * Main loop - read until there ain't no more. Note codec
	 * samples are bit-inverted.
	 */
	DTOLFP((double)rbufp->recv_length / WWV_SEC, &ltemp);
	L_SUB(&rbufp->recv_time, &ltemp);
	up->timestamp = rbufp->recv_time;
	dpt = rbufp->recv_buffer;
	for (bufcnt = 0; bufcnt < rbufp->recv_length; bufcnt++) {
		sample = up->comp[~*dpt++ & 0xff];

		/*
		 * Clip noise spikes greater than MAXAMP (6000) and
		 * record the number of clips to be used later by the
		 * AGC.
		 */
		if (sample > MAXAMP) {
			sample = MAXAMP;
			up->clipcnt++;
		} else if (sample < -MAXAMP) {
			sample = -MAXAMP;
			up->clipcnt++;
		}

		/*
		 * Variable frequency oscillator. The codec oscillator
		 * runs at the nominal rate of 8000 samples per second,
		 * or 125 us per sample. A frequency change of one unit
		 * results in either duplicating or deleting one sample
		 * per second, which results in a frequency change of
		 * 125 PPM.
		 */
		up->phase += (up->freq + clock_codec) / WWV_SEC;
		if (up->phase >= .5) {
			up->phase -= 1.;
		} else if (up->phase < -.5) {
			up->phase += 1.;
			wwv_rf(peer, sample);
			wwv_rf(peer, sample);
		} else {
			wwv_rf(peer, sample);
		}
		L_ADD(&up->timestamp, &up->tick);
	}

	/*
	 * Set the input port and monitor gain for the next buffer.
	 */
	if (pp->sloppyclockflag & CLK_FLAG2)
		up->port = 2;
	else
		up->port = 1;
	if (pp->sloppyclockflag & CLK_FLAG3)
		up->mongain = MONGAIN;
	else
		up->mongain = 0;
}


/*
 * wwv_poll - called by the transmit procedure
 *
 * This routine keeps track of status. If no offset samples have been
 * processed during a poll interval, a timeout event is declared. If
 * errors have have occurred during the interval, they are reported as
 * well.
 */
static void
wwv_poll(
	int	unit,		/* instance number (not used) */
	struct peer *peer	/* peer structure pointer */
	)
{
	struct refclockproc *pp;
	struct wwvunit *up;

	pp = peer->procptr;
	up = pp->unitptr;
	if (up->errflg)
		refclock_report(peer, up->errflg);
	up->errflg = 0;
	pp->polls++;
}


/*
 * wwv_rf - process signals and demodulate to baseband
 *
 * This routine grooms and filters decompanded raw audio samples. The
 * output signal is the 100-Hz filtered baseband data signal in
 * quadrature phase. The routine also determines the minute synch epoch,
 * as well as certain signal maxima, minima and related values.
 *
 * There are two 1-s ramps used by this program. Both count the 8000
 * logical clock samples spanning exactly one second. The epoch ramp
 * counts the samples starting at an arbitrary time. The rphase ramp
 * counts the samples starting at the 5-ms second sync pulse found
 * during the epoch ramp.
 *
 * There are two 1-m ramps used by this program. The mphase ramp counts
 * the 480,000 logical clock samples spanning exactly one minute and
 * starting at an arbitrary time. The rsec ramp counts the 60 seconds of
 * the minute starting at the 800-ms minute sync pulse found during the
 * mphase ramp. The rsec ramp drives the seconds state machine to
 * determine the bits and digits of the timecode. 
 *
 * Demodulation operations are based on three synthesized quadrature
 * sinusoids: 100 Hz for the data signal, 1000 Hz for the WWV sync
 * signal and 1200 Hz for the WWVH sync signal. These drive synchronous
 * matched filters for the data signal (170 ms at 100 Hz), WWV minute
 * sync signal (800 ms at 1000 Hz) and WWVH minute sync signal (800 ms
 * at 1200 Hz). Two additional matched filters are switched in
 * as required for the WWV second sync signal (5 cycles at 1000 Hz) and
 * WWVH second sync signal (6 cycles at 1200 Hz).
 */
static void
wwv_rf(
	struct peer *peer,	/* peerstructure pointer */
	double isig		/* input signal */
	)
{
	struct refclockproc *pp;
	struct wwvunit *up;
	struct sync *sp, *rp;

	static double lpf[5];	/* 150-Hz lpf delay line */
	double data;		/* lpf output */
	static double bpf[9];	/* 1000/1200-Hz bpf delay line */
	double syncx;		/* bpf output */
	static double mf[41];	/* 1000/1200-Hz mf delay line */
	double mfsync;		/* mf output */

	static int iptr;	/* data channel pointer */
	static double ibuf[DATSIZ]; /* data I channel delay line */
	static double qbuf[DATSIZ]; /* data Q channel delay line */

	static int jptr;	/* sync channel pointer */
	static int kptr;	/* tick channel pointer */

	static int csinptr;	/* wwv channel phase */
	static double cibuf[SYNSIZ]; /* wwv I channel delay line */
	static double cqbuf[SYNSIZ]; /* wwv Q channel delay line */
	static double ciamp;	/* wwv I channel amplitude */
	static double cqamp;	/* wwv Q channel amplitude */

	static double csibuf[TCKSIZ]; /* wwv I tick delay line */
	static double csqbuf[TCKSIZ]; /* wwv Q tick delay line */
	static double csiamp;	/* wwv I tick amplitude */
	static double csqamp;	/* wwv Q tick amplitude */

	static int hsinptr;	/* wwvh channel phase */
	static double hibuf[SYNSIZ]; /* wwvh I channel delay line */
	static double hqbuf[SYNSIZ]; /* wwvh Q channel delay line */
	static double hiamp;	/* wwvh I channel amplitude */
	static double hqamp;	/* wwvh Q channel amplitude */

	static double hsibuf[TCKSIZ]; /* wwvh I tick delay line */
	static double hsqbuf[TCKSIZ]; /* wwvh Q tick delay line */
	static double hsiamp;	/* wwvh I tick amplitude */
	static double hsqamp;	/* wwvh Q tick amplitude */

	static double epobuf[WWV_SEC]; /* second sync comb filter */
	static double epomax, nxtmax; /* second sync amplitude buffer */
	static int epopos;	/* epoch second sync position buffer */

	static int iniflg;	/* initialization flag */
	int	epoch;		/* comb filter index */
	double	dtemp;
	int	i;

	pp = peer->procptr;
	up = pp->unitptr;

	if (!iniflg) {
		iniflg = 1;
		memset((char *)lpf, 0, sizeof(lpf));
		memset((char *)bpf, 0, sizeof(bpf));
		memset((char *)mf, 0, sizeof(mf));
		memset((char *)ibuf, 0, sizeof(ibuf));
		memset((char *)qbuf, 0, sizeof(qbuf));
		memset((char *)cibuf, 0, sizeof(cibuf));
		memset((char *)cqbuf, 0, sizeof(cqbuf));
		memset((char *)csibuf, 0, sizeof(csibuf));
		memset((char *)csqbuf, 0, sizeof(csqbuf));
		memset((char *)hibuf, 0, sizeof(hibuf));
		memset((char *)hqbuf, 0, sizeof(hqbuf));
		memset((char *)hsibuf, 0, sizeof(hsibuf));
		memset((char *)hsqbuf, 0, sizeof(hsqbuf));
		memset((char *)epobuf, 0, sizeof(epobuf));
	}

	/*
	 * Baseband data demodulation. The 100-Hz subcarrier is
	 * extracted using a 150-Hz IIR lowpass filter. This attenuates
	 * the 1000/1200-Hz sync signals, as well as the 440-Hz and
	 * 600-Hz tones and most of the noise and voice modulation
	 * components.
	 *
	 * The subcarrier is transmitted 10 dB down from the carrier.
	 * The DGAIN parameter can be adjusted for this and to
	 * compensate for the radio audio response at 100 Hz.
	 *
	 * Matlab IIR 4th-order IIR elliptic, 150 Hz lowpass, 0.2 dB
	 * passband ripple, -50 dB stopband ripple, phase delay 0.97 ms.
	 */
	data = (lpf[4] = lpf[3]) * 8.360961e-01;
	data += (lpf[3] = lpf[2]) * -3.481740e+00;
	data += (lpf[2] = lpf[1]) * 5.452988e+00;
	data += (lpf[1] = lpf[0]) * -3.807229e+00;
	lpf[0] = isig * DGAIN - data;
	data = lpf[0] * 3.281435e-03
	    + lpf[1] * -1.149947e-02
	    + lpf[2] * 1.654858e-02
	    + lpf[3] * -1.149947e-02
	    + lpf[4] * 3.281435e-03;

	/*
	 * The 100-Hz data signal is demodulated using a pair of
	 * quadrature multipliers, matched filters and a phase lock
	 * loop. The I and Q quadrature data signals are produced by
	 * multiplying the filtered signal by 100-Hz sine and cosine
	 * signals, respectively. The signals are processed by 170-ms
	 * synchronous matched filters to produce the amplitude and
	 * phase signals used by the demodulator. The signals are scaled
	 * to produce unit energy at the maximum value.
	 */
	i = up->datapt;
	up->datapt = (up->datapt + IN100) % 80;
	dtemp = sintab[i] * data / (MS / 2. * DATCYC);
	up->irig -= ibuf[iptr];
	ibuf[iptr] = dtemp;
	up->irig += dtemp;

	i = (i + 20) % 80;
	dtemp = sintab[i] * data / (MS / 2. * DATCYC);
	up->qrig -= qbuf[iptr];
	qbuf[iptr] = dtemp;
	up->qrig += dtemp;
	iptr = (iptr + 1) % DATSIZ;

	/*
	 * Baseband sync demodulation. The 1000/1200 sync signals are
	 * extracted using a 600-Hz IIR bandpass filter. This removes
	 * the 100-Hz data subcarrier, as well as the 440-Hz and 600-Hz
	 * tones and most of the noise and voice modulation components.
	 *
	 * Matlab 4th-order IIR elliptic, 800-1400 Hz bandpass, 0.2 dB
	 * passband ripple, -50 dB stopband ripple, phase delay 0.91 ms.
	 */
	syncx = (bpf[8] = bpf[7]) * 4.897278e-01;
	syncx += (bpf[7] = bpf[6]) * -2.765914e+00;
	syncx += (bpf[6] = bpf[5]) * 8.110921e+00;
	syncx += (bpf[5] = bpf[4]) * -1.517732e+01;
	syncx += (bpf[4] = bpf[3]) * 1.975197e+01;
	syncx += (bpf[3] = bpf[2]) * -1.814365e+01;
	syncx += (bpf[2] = bpf[1]) * 1.159783e+01;
	syncx += (bpf[1] = bpf[0]) * -4.735040e+00;
	bpf[0] = isig - syncx;
	syncx = bpf[0] * 8.203628e-03
	    + bpf[1] * -2.375732e-02
	    + bpf[2] * 3.353214e-02
	    + bpf[3] * -4.080258e-02
	    + bpf[4] * 4.605479e-02
	    + bpf[5] * -4.080258e-02
	    + bpf[6] * 3.353214e-02
	    + bpf[7] * -2.375732e-02
	    + bpf[8] * 8.203628e-03;

	/*
	 * The 1000/1200 sync signals are demodulated using a pair of
	 * quadrature multipliers and matched filters. However,
	 * synchronous demodulation at these frequencies is impractical,
	 * so only the signal amplitude is used. The I and Q quadrature
	 * sync signals are produced by multiplying the filtered signal
	 * by 1000-Hz (WWV) and 1200-Hz (WWVH) sine and cosine signals,
	 * respectively. The WWV and WWVH signals are processed by 800-
	 * ms synchronous matched filters and combined to produce the
	 * minute sync signal and detect which one (or both) the WWV or
	 * WWVH signal is present. The WWV and WWVH signals are also
	 * processed by 5-ms synchronous matched filters and combined to
	 * produce the second sync signal. The signals are scaled to
	 * produce unit energy at the maximum value.
	 *
	 * Note the master timing ramps, which run continuously. The
	 * minute counter (mphase) counts the samples in the minute,
	 * while the second counter (epoch) counts the samples in the
	 * second.
	 */
	up->mphase = (up->mphase + 1) % WWV_MIN;
	epoch = up->mphase % WWV_SEC;

	/*
	 * WWV
	 */
	i = csinptr;
	csinptr = (csinptr + IN1000) % 80;

	dtemp = sintab[i] * syncx / (MS / 2.);
	ciamp -= cibuf[jptr];
	cibuf[jptr] = dtemp;
	ciamp += dtemp;
	csiamp -= csibuf[kptr];
	csibuf[kptr] = dtemp;
	csiamp += dtemp;

	i = (i + 20) % 80;
	dtemp = sintab[i] * syncx / (MS / 2.);
	cqamp -= cqbuf[jptr];
	cqbuf[jptr] = dtemp;
	cqamp += dtemp;
	csqamp -= csqbuf[kptr];
	csqbuf[kptr] = dtemp;
	csqamp += dtemp;

	sp = &up->mitig[up->achan].wwv;
	sp->amp = sqrt(ciamp * ciamp + cqamp * cqamp) / SYNCYC;
	if (!(up->status & MSYNC))
		wwv_qrz(peer, sp, (int)(pp->fudgetime1 * WWV_SEC));

	/*
	 * WWVH
	 */
	i = hsinptr;
	hsinptr = (hsinptr + IN1200) % 80;

	dtemp = sintab[i] * syncx / (MS / 2.);
	hiamp -= hibuf[jptr];
	hibuf[jptr] = dtemp;
	hiamp += dtemp;
	hsiamp -= hsibuf[kptr];
	hsibuf[kptr] = dtemp;
	hsiamp += dtemp;

	i = (i + 20) % 80;
	dtemp = sintab[i] * syncx / (MS / 2.);
	hqamp -= hqbuf[jptr];
	hqbuf[jptr] = dtemp;
	hqamp += dtemp;
	hsqamp -= hsqbuf[kptr];
	hsqbuf[kptr] = dtemp;
	hsqamp += dtemp;

	rp = &up->mitig[up->achan].wwvh;
	rp->amp = sqrt(hiamp * hiamp + hqamp * hqamp) / SYNCYC;
	if (!(up->status & MSYNC))
		wwv_qrz(peer, rp, (int)(pp->fudgetime2 * WWV_SEC));
	jptr = (jptr + 1) % SYNSIZ;
	kptr = (kptr + 1) % TCKSIZ;

	/*
	 * The following section is called once per minute. It does
	 * housekeeping and timeout functions and empties the dustbins.
	 */
	if (up->mphase == 0) {
		up->watch++;
		if (!(up->status & MSYNC)) {

			/*
			 * If minute sync has not been acquired before
			 * ACQSN timeout (6 min), or if no signal is
			 * heard, the program cycles to the next
			 * frequency and tries again.
			 */
			if (!wwv_newchan(peer))
				up->watch = 0;
		} else {

			/*
			 * If the leap bit is set, set the minute epoch
			 * back one second so the station processes
			 * don't miss a beat.
			 */
			if (up->status & LEPSEC) {
				up->mphase -= WWV_SEC;
				if (up->mphase < 0)
					up->mphase += WWV_MIN;
			}
		}
	}

	/*
	 * When the channel metric reaches threshold and the second
	 * counter matches the minute epoch within the second, the
	 * driver has synchronized to the station. The second number is
	 * the remaining seconds until the next minute epoch, while the
	 * sync epoch is zero. Watch out for the first second; if
	 * already synchronized to the second, the buffered sync epoch
	 * must be set.
	 *
	 * Note the guard interval is 200 ms; if for some reason the
	 * clock drifts more than that, it might wind up in the wrong
	 * second. If the maximum frequency error is not more than about
	 * 1 PPM, the clock can go as much as two days while still in
	 * the same second.
	 */
	if (up->status & MSYNC) {
		wwv_epoch(peer);
	} else if (up->sptr != NULL) {
		sp = up->sptr;
		if (sp->metric >= TTHR && epoch == sp->mepoch % WWV_SEC)
 		    {
			up->rsec = (60 - sp->mepoch / WWV_SEC) % 60;
			up->rphase = 0;
			up->status |= MSYNC;
			up->watch = 0;
			if (!(up->status & SSYNC))
				up->repoch = up->yepoch = epoch;
			else
				up->repoch = up->yepoch;
			
		}
	}

	/*
	 * The second sync pulse is extracted using 5-ms (40 sample) FIR
	 * matched filters at 1000 Hz for WWV or 1200 Hz for WWVH. This
	 * pulse is used for the most precise synchronization, since if
	 * provides a resolution of one sample (125 us). The filters run
	 * only if the station has been reliably determined.
	 */
	if (up->status & SELV)
		mfsync = sqrt(csiamp * csiamp + csqamp * csqamp) /
		    TCKCYC;
	else if (up->status & SELH)
		mfsync = sqrt(hsiamp * hsiamp + hsqamp * hsqamp) /
		    TCKCYC;
	else
		mfsync = 0;

	/*
	 * Enhance the seconds sync pulse using a 1-s (8000-sample) comb
	 * filter. Correct for the FIR matched filter delay, which is 5
	 * ms for both the WWV and WWVH filters, and also for the
	 * propagation delay. Once each second look for second sync. If
	 * not in minute sync, fiddle the codec gain. Note the SNR is
	 * computed from the maximum sample and the envelope of the
	 * sample 6 ms before it, so if we slip more than a cycle the
	 * SNR should plummet. The signal is scaled to produce unit
	 * energy at the maximum value.
	 */
	dtemp = (epobuf[epoch] += (mfsync - epobuf[epoch]) /
	    up->avgint);
	if (dtemp > epomax) {
		int	j;

		epomax = dtemp;
		epopos = epoch;
		j = epoch - 6 * MS;
		if (j < 0)
			j += WWV_SEC;
		nxtmax = fabs(epobuf[j]);
	}
	if (epoch == 0) {
		up->epomax = epomax;
		up->eposnr = wwv_snr(epomax, nxtmax);
		epopos -= TCKCYC * MS;
		if (epopos < 0)
			epopos += WWV_SEC;
		wwv_endpoc(peer, epopos);
		if (!(up->status & SSYNC))
			up->alarm |= SYNERR;
		epomax = 0;
		if (!(up->status & MSYNC))
			wwv_gain(peer);
	}
}


/*
 * wwv_qrz - identify and acquire WWV/WWVH minute sync pulse
 *
 * This routine implements a virtual station process used to acquire
 * minute sync and to mitigate among the ten frequency and station
 * combinations. During minute sync acquisition the process probes each
 * frequency and station in turn for the minute pulse, which
 * involves searching through the entire 480,000-sample minute. The
 * process finds the maximum signal and RMS noise plus signal. Then, the
 * actual noise is determined by subtracting the energy of the matched
 * filter.
 *
 * Students of radar receiver technology will discover this algorithm
 * amounts to a range-gate discriminator. A valid pulse must have peak
 * amplitude at least QTHR (2500) and SNR at least QSNR (20) dB and the
 * difference between the current and previous epoch must be less than
 * AWND (20 ms). Note that the discriminator peak occurs about 800 ms
 * into the second, so the timing is retarded to the previous second
 * epoch.
 */
static void
wwv_qrz(
	struct peer *peer,	/* peer structure pointer */
	struct sync *sp,	/* sync channel structure */
	int	pdelay		/* propagation delay (samples) */
	)
{
	struct refclockproc *pp;
	struct wwvunit *up;
	char	tbuf[TBUF];	/* monitor buffer */
	long	epoch;

	pp = peer->procptr;
	up = pp->unitptr;

	/*
	 * Find the sample with peak amplitude, which defines the minute
	 * epoch. Accumulate all samples to determine the total noise
	 * energy.
	 */
	epoch = up->mphase - pdelay - SYNSIZ;
	if (epoch < 0)
		epoch += WWV_MIN;
	if (sp->amp > sp->maxeng) {
		sp->maxeng = sp->amp;
		sp->pos = epoch;
	}
	sp->noieng += sp->amp;

	/*
	 * At the end of the minute, determine the epoch of the minute
	 * sync pulse, as well as the difference between the current and
	 * previous epoches due to the intrinsic frequency error plus
	 * jitter. When calculating the SNR, subtract the pulse energy
	 * from the total noise energy and then normalize.
	 */
	if (up->mphase == 0) {
		sp->synmax = sp->maxeng;
		sp->synsnr = wwv_snr(sp->synmax, (sp->noieng -
		    sp->synmax) / WWV_MIN);
		if (sp->count == 0)
			sp->lastpos = sp->pos;
		epoch = (sp->pos - sp->lastpos) % WWV_MIN;
		sp->reach <<= 1;
		if (sp->reach & (1 << AMAX))
			sp->count--;
		if (sp->synmax > ATHR && sp->synsnr > ASNR) {
			if (labs(epoch) < AWND * MS) {
				sp->reach |= 1;
				sp->count++;
				sp->mepoch = sp->lastpos = sp->pos;
			} else if (sp->count == 1) {
				sp->lastpos = sp->pos;
			}
		}
		if (up->watch > ACQSN)
			sp->metric = 0;
		else
			sp->metric = wwv_metric(sp);
		if (pp->sloppyclockflag & CLK_FLAG4) {
			snprintf(tbuf, sizeof(tbuf),
			    "wwv8 %04x %3d %s %04x %.0f %.0f/%.1f %ld %ld",
			    up->status, up->gain, sp->refid,
			    sp->reach & 0xffff, sp->metric, sp->synmax,
			    sp->synsnr, sp->pos % WWV_SEC, epoch);
			record_clock_stats(&peer->srcadr, tbuf);
#ifdef DEBUG
			if (debug)
				printf("%s\n", tbuf);
#endif /* DEBUG */
		}
		sp->maxeng = sp->noieng = 0;
	}
}


/*
 * wwv_endpoc - identify and acquire second sync pulse
 *
 * This routine is called at the end of the second sync interval. It
 * determines the second sync epoch position within the second and
 * disciplines the sample clock using a frequency-lock loop (FLL).
 *
 * Second sync is determined in the RF input routine as the maximum
 * over all 8000 samples in the second comb filter. To assure accurate
 * and reliable time and frequency discipline, this routine performs a
 * great deal of heavy-handed heuristic data filtering and grooming.
 */
static void
wwv_endpoc(
	struct peer *peer,	/* peer structure pointer */
	int epopos		/* epoch max position */
	)
{
	struct refclockproc *pp;
	struct wwvunit *up;
	static int epoch_mf[3]; /* epoch median filter */
	static int tepoch;	/* current second epoch */
 	static int xepoch;	/* last second epoch */
 	static int zepoch;	/* last run epoch */
	static int zcount;	/* last run end time */
	static int scount;	/* seconds counter */
	static int syncnt;	/* run length counter */
	static int maxrun;	/* longest run length */
	static int mepoch;	/* longest run end epoch */
	static int mcount;	/* longest run end time */
	static int avgcnt;	/* averaging interval counter */
	static int avginc;	/* averaging ratchet */
	static int iniflg;	/* initialization flag */
	char tbuf[TBUF];		/* monitor buffer */
	double dtemp;
	int tmp2;

	pp = peer->procptr;
	up = pp->unitptr;
	if (!iniflg) {
		iniflg = 1;
		ZERO(epoch_mf);
	}

	/*
	 * If the signal amplitude or SNR fall below thresholds, dim the
	 * second sync lamp and wait for hotter ions. If no stations are
	 * heard, we are either in a probe cycle or the ions are really
	 * cold. 
	 */
	scount++;
	if (up->epomax < STHR || up->eposnr < SSNR) {
		up->status &= ~(SSYNC | FGATE);
		avgcnt = syncnt = maxrun = 0;
		return;
	}
	if (!(up->status & (SELV | SELH)))
		return;

	/*
	 * A three-stage median filter is used to help denoise the
	 * second sync pulse. The median sample becomes the candidate
	 * epoch.
	 */
	epoch_mf[2] = epoch_mf[1];
	epoch_mf[1] = epoch_mf[0];
	epoch_mf[0] = epopos;
	if (epoch_mf[0] > epoch_mf[1]) {
		if (epoch_mf[1] > epoch_mf[2])
			tepoch = epoch_mf[1];	/* 0 1 2 */
		else if (epoch_mf[2] > epoch_mf[0])
			tepoch = epoch_mf[0];	/* 2 0 1 */
		else
			tepoch = epoch_mf[2];	/* 0 2 1 */
	} else {
		if (epoch_mf[1] < epoch_mf[2])
			tepoch = epoch_mf[1];	/* 2 1 0 */
		else if (epoch_mf[2] < epoch_mf[0])
			tepoch = epoch_mf[0];	/* 1 0 2 */
		else
			tepoch = epoch_mf[2];	/* 1 2 0 */
	}


	/*
	 * If the epoch candidate is the same as the last one, increment
	 * the run counter. If not, save the length, epoch and end
	 * time of the current run for use later and reset the counter.
	 * The epoch is considered valid if the run is at least SCMP
	 * (10) s, the minute is synchronized and the interval since the
	 * last epoch  is not greater than the averaging interval. Thus,
	 * after a long absence, the program will wait a full averaging
	 * interval while the comb filter charges up and noise
	 * dissapates..
	 */
	tmp2 = (tepoch - xepoch) % WWV_SEC;
	if (tmp2 == 0) {
		syncnt++;
		if (syncnt > SCMP && up->status & MSYNC && (up->status &
		    FGATE || scount - zcount <= up->avgint)) {
			up->status |= SSYNC;
			up->yepoch = tepoch;
		}
	} else if (syncnt >= maxrun) {
		maxrun = syncnt;
		mcount = scount;
		mepoch = xepoch;
		syncnt = 0;
	}
	if ((pp->sloppyclockflag & CLK_FLAG4) && !(up->status &
	    MSYNC)) {
		snprintf(tbuf, sizeof(tbuf),
		    "wwv1 %04x %3d %4d %5.0f %5.1f %5d %4d %4d %4d",
		    up->status, up->gain, tepoch, up->epomax,
		    up->eposnr, tmp2, avgcnt, syncnt,
		    maxrun);
		record_clock_stats(&peer->srcadr, tbuf);
#ifdef DEBUG
		if (debug)
			printf("%s\n", tbuf);
#endif /* DEBUG */
	}
	avgcnt++;
	if (avgcnt < up->avgint) {
		xepoch = tepoch;
		return;
	}

	/*
	 * The sample clock frequency is disciplined using a first-order
	 * feedback loop with time constant consistent with the Allan
	 * intercept of typical computer clocks. During each averaging
	 * interval the candidate epoch at the end of the longest run is
	 * determined. If the longest run is zero, all epoches in the
	 * interval are different, so the candidate epoch is the current
	 * epoch. The frequency update is computed from the candidate
	 * epoch difference (125-us units) and time difference (seconds)
	 * between updates.
	 */
	if (syncnt >= maxrun) {
		maxrun = syncnt;
		mcount = scount;
		mepoch = xepoch;
	}
	xepoch = tepoch;
	if (maxrun == 0) {
		mepoch = tepoch;
		mcount = scount;
	}

	/*
	 * The master clock runs at the codec sample frequency of 8000
	 * Hz, so the intrinsic time resolution is 125 us. The frequency
	 * resolution ranges from 18 PPM at the minimum averaging
	 * interval of 8 s to 0.12 PPM at the maximum interval of 1024
	 * s. An offset update is determined at the end of the longest
	 * run in each averaging interval. The frequency adjustment is
	 * computed from the difference between offset updates and the
	 * interval between them.
	 *
	 * The maximum frequency adjustment ranges from 187 PPM at the
	 * minimum interval to 1.5 PPM at the maximum. If the adjustment
	 * exceeds the maximum, the update is discarded and the
	 * hysteresis counter is decremented. Otherwise, the frequency
	 * is incremented by the adjustment, but clamped to the maximum
	 * 187.5 PPM. If the update is less than half the maximum, the
	 * hysteresis counter is incremented. If the counter increments
	 * to +3, the averaging interval is doubled and the counter set
	 * to zero; if it decrements to -3, the interval is halved and
	 * the counter set to zero.
	 */
	dtemp = (mepoch - zepoch) % WWV_SEC;
	if (up->status & FGATE) {
		if (fabs(dtemp) < MAXFREQ * MINAVG) {
			up->freq += (dtemp / 2.) / ((mcount - zcount) *
			    FCONST);
			if (up->freq > MAXFREQ)
				up->freq = MAXFREQ;
			else if (up->freq < -MAXFREQ)
				up->freq = -MAXFREQ;
			if (fabs(dtemp) < MAXFREQ * MINAVG / 2.) {
				if (avginc < 3) {
					avginc++;
				} else {
					if (up->avgint < MAXAVG) {
						up->avgint <<= 1;
						avginc = 0;
					}
				}
			}
		} else {
			if (avginc > -3) {
				avginc--;
			} else {
				if (up->avgint > MINAVG) {
					up->avgint >>= 1;
					avginc = 0;
				}
			}
		}
	}
	if (pp->sloppyclockflag & CLK_FLAG4) {
		snprintf(tbuf, sizeof(tbuf),
		    "wwv2 %04x %5.0f %5.1f %5d %4d %4d %4d %4.0f %7.2f",
		    up->status, up->epomax, up->eposnr, mepoch,
		    up->avgint, maxrun, mcount - zcount, dtemp,
		    up->freq * 1e6 / WWV_SEC);
		record_clock_stats(&peer->srcadr, tbuf);
#ifdef DEBUG
		if (debug)
			printf("%s\n", tbuf);
#endif /* DEBUG */
	}

	/*
	 * This is a valid update; set up for the next interval.
	 */
	up->status |= FGATE;
	zepoch = mepoch;
	zcount = mcount;
	avgcnt = syncnt = maxrun = 0;
}


/*
 * wwv_epoch - epoch scanner
 *
 * This routine extracts data signals from the 100-Hz subcarrier. It
 * scans the receiver second epoch to determine the signal amplitudes
 * and pulse timings. Receiver synchronization is determined by the
 * minute sync pulse detected in the wwv_rf() routine and the second
 * sync pulse detected in the wwv_epoch() routine. The transmitted
 * signals are delayed by the propagation delay, receiver delay and
 * filter delay of this program. Delay corrections are introduced
 * separately for WWV and WWVH. 
 *
 * Most communications radios use a highpass filter in the audio stages,
 * which can do nasty things to the subcarrier phase relative to the
 * sync pulses. Therefore, the data subcarrier reference phase is
 * disciplined using the hardlimited quadrature-phase signal sampled at
 * the same time as the in-phase signal. The phase tracking loop uses
 * phase adjustments of plus-minus one sample (125 us). 
 */
static void
wwv_epoch(
	struct peer *peer	/* peer structure pointer */
	)
{
	struct refclockproc *pp;
	struct wwvunit *up;
	struct chan *cp;
	static double sigmin, sigzer, sigone, engmax, engmin;

	pp = peer->procptr;
	up = pp->unitptr;

	/*
	 * Find the maximum minute sync pulse energy for both the
	 * WWV and WWVH stations. This will be used later for channel
	 * and station mitigation. Also set the seconds epoch at 800 ms
	 * well before the end of the second to make sure we never set
	 * the epoch backwards.
	 */
	cp = &up->mitig[up->achan];
	if (cp->wwv.amp > cp->wwv.syneng) 
		cp->wwv.syneng = cp->wwv.amp;
	if (cp->wwvh.amp > cp->wwvh.syneng) 
		cp->wwvh.syneng = cp->wwvh.amp;
	if (up->rphase == 800 * MS)
		up->repoch = up->yepoch;

	/*
	 * Use the signal amplitude at epoch 15 ms as the noise floor.
	 * This gives a guard time of +-15 ms from the beginning of the
	 * second until the second pulse rises at 30 ms. There is a
	 * compromise here; we want to delay the sample as long as
	 * possible to give the radio time to change frequency and the
	 * AGC to stabilize, but as early as possible if the second
	 * epoch is not exact.
	 */
	if (up->rphase == 15 * MS)
		sigmin = sigzer = sigone = up->irig;

	/*
	 * Latch the data signal at 200 ms. Keep this around until the
	 * end of the second. Use the signal energy as the peak to
	 * compute the SNR. Use the Q sample to adjust the 100-Hz
	 * reference oscillator phase.
	 */
	if (up->rphase == 200 * MS) {
		sigzer = up->irig;
		engmax = sqrt(up->irig * up->irig + up->qrig *
		    up->qrig);
		up->datpha = up->qrig / up->avgint;
		if (up->datpha >= 0) {
			up->datapt++;
			if (up->datapt >= 80)
				up->datapt -= 80;
		} else {
			up->datapt--;
			if (up->datapt < 0)
				up->datapt += 80;
		}
	}


	/*
	 * Latch the data signal at 500 ms. Keep this around until the
	 * end of the second.
	 */
	else if (up->rphase == 500 * MS)
		sigone = up->irig;

	/*
	 * At the end of the second crank the clock state machine and
	 * adjust the codec gain. Note the epoch is buffered from the
	 * center of the second in order to avoid jitter while the
	 * seconds synch is diddling the epoch. Then, determine the true
	 * offset and update the median filter in the driver interface.
	 *
	 * Use the energy at the end of the second as the noise to
	 * compute the SNR for the data pulse. This gives a better
	 * measurement than the beginning of the second, especially when
	 * returning from the probe channel. This gives a guard time of
	 * 30 ms from the decay of the longest pulse to the rise of the
	 * next pulse.
	 */
	up->rphase++;
	if (up->mphase % WWV_SEC == up->repoch) {
		up->status &= ~(DGATE | BGATE);
		engmin = sqrt(up->irig * up->irig + up->qrig *
		    up->qrig);
		up->datsig = engmax;
		up->datsnr = wwv_snr(engmax, engmin);

		/*
		 * If the amplitude or SNR is below threshold, average a
		 * 0 in the the integrators; otherwise, average the
		 * bipolar signal. This is done to avoid noise polution.
		 */
		if (engmax < DTHR || up->datsnr < DSNR) {
			up->status |= DGATE;
			wwv_rsec(peer, 0);
		} else {
			sigzer -= sigone;
			sigone -= sigmin;
			wwv_rsec(peer, sigone - sigzer);
		}
		if (up->status & (DGATE | BGATE))
			up->errcnt++;
		if (up->errcnt > MAXERR)
			up->alarm |= LOWERR;
		wwv_gain(peer);
		cp = &up->mitig[up->achan];
		cp->wwv.syneng = 0;
		cp->wwvh.syneng = 0;
		up->rphase = 0;
	}
}


/*
 * wwv_rsec - process receiver second
 *
 * This routine is called at the end of each receiver second to
 * implement the per-second state machine. The machine assembles BCD
 * digit bits, decodes miscellaneous bits and dances the leap seconds.
 *
 * Normally, the minute has 60 seconds numbered 0-59. If the leap
 * warning bit is set, the last minute (1439) of 30 June (day 181 or 182
 * for leap years) or 31 December (day 365 or 366 for leap years) is
 * augmented by one second numbered 60. This is accomplished by
 * extending the minute interval by one second and teaching the state
 * machine to ignore it.
 */
static void
wwv_rsec(
	struct peer *peer,	/* peer structure pointer */
	double bit
	)
{
	static int iniflg;	/* initialization flag */
	static double bcddld[4]; /* BCD data bits */
	static double bitvec[61]; /* bit integrator for misc bits */
	struct refclockproc *pp;
	struct wwvunit *up;
	struct chan *cp;
	struct sync *sp, *rp;
	char	tbuf[TBUF];	/* monitor buffer */
	int	sw, arg, nsec;

	pp = peer->procptr;
	up = pp->unitptr;
	if (!iniflg) {
		iniflg = 1;
		ZERO(bitvec);
	}

	/*
	 * The bit represents the probability of a hit on zero (negative
	 * values), a hit on one (positive values) or a miss (zero
	 * value). The likelihood vector is the exponential average of
	 * these probabilities. Only the bits of this vector
	 * corresponding to the miscellaneous bits of the timecode are
	 * used, but it's easier to do them all. After that, crank the
	 * seconds state machine.
	 */
	nsec = up->rsec;
	up->rsec++;
	bitvec[nsec] += (bit - bitvec[nsec]) / TCONST;
	sw = progx[nsec].sw;
	arg = progx[nsec].arg;

	/*
	 * The minute state machine. Fly off to a particular section as
	 * directed by the transition matrix and second number.
	 */
	switch (sw) {

	/*
	 * Ignore this second.
	 */
	case IDLE:			/* 9, 45-49 */
		break;

	/*
	 * Probe channel stuff
	 *
	 * The WWV/H format contains data pulses in second 59 (position
	 * identifier) and second 1, but not in second 0. The minute
	 * sync pulse is contained in second 0. At the end of second 58
	 * QSY to the probe channel, which rotates in turn over all
	 * WWV/H frequencies. At the end of second 0 measure the minute
	 * sync pulse. At the end of second 1 measure the data pulse and
	 * QSY back to the data channel. Note that the actions commented
	 * here happen at the end of the second numbered as shown.
	 *
	 * At the end of second 0 save the minute sync amplitude latched
	 * at 800 ms as the signal later used to calculate the SNR. 
	 */
	case SYNC2:			/* 0 */
		cp = &up->mitig[up->achan];
		cp->wwv.synmax = cp->wwv.syneng;
		cp->wwvh.synmax = cp->wwvh.syneng;
		break;

	/*
	 * At the end of second 1 use the minute sync amplitude latched
	 * at 800 ms as the noise to calculate the SNR. If the minute
	 * sync pulse and SNR are above thresholds and the data pulse
	 * amplitude and SNR are above thresolds, shift a 1 into the
	 * station reachability register; otherwise, shift a 0. The
	 * number of 1 bits in the last six intervals is a component of
	 * the channel metric computed by the wwv_metric() routine.
	 * Finally, QSY back to the data channel.
	 */
	case SYNC3:			/* 1 */
		cp = &up->mitig[up->achan];

		/*
		 * WWV station
		 */
		sp = &cp->wwv;
		sp->synsnr = wwv_snr(sp->synmax, sp->amp);
		sp->reach <<= 1;
		if (sp->reach & (1 << AMAX))
			sp->count--;
		if (sp->synmax >= QTHR && sp->synsnr >= QSNR &&
		    !(up->status & (DGATE | BGATE))) {
			sp->reach |= 1;
			sp->count++;
		}
		sp->metric = wwv_metric(sp);

		/*
		 * WWVH station
		 */
		rp = &cp->wwvh;
		rp->synsnr = wwv_snr(rp->synmax, rp->amp);
		rp->reach <<= 1;
		if (rp->reach & (1 << AMAX))
			rp->count--;
		if (rp->synmax >= QTHR && rp->synsnr >= QSNR &&
		    !(up->status & (DGATE | BGATE))) {
			rp->reach |= 1;
			rp->count++;
		}
		rp->metric = wwv_metric(rp);
		if (pp->sloppyclockflag & CLK_FLAG4) {
			snprintf(tbuf, sizeof(tbuf),
			    "wwv5 %04x %3d %4d %.0f/%.1f %.0f/%.1f %s %04x %.0f %.0f/%.1f %s %04x %.0f %.0f/%.1f",
			    up->status, up->gain, up->yepoch,
			    up->epomax, up->eposnr, up->datsig,
			    up->datsnr,
			    sp->refid, sp->reach & 0xffff,
			    sp->metric, sp->synmax, sp->synsnr,
			    rp->refid, rp->reach & 0xffff,
			    rp->metric, rp->synmax, rp->synsnr);
			record_clock_stats(&peer->srcadr, tbuf);
#ifdef DEBUG
			if (debug)
				printf("%s\n", tbuf);
#endif /* DEBUG */
		}
		up->errcnt = up->digcnt = up->alarm = 0;

		/*
		 * If synchronized to a station, restart if no stations
		 * have been heard within the PANIC timeout (2 days). If
		 * not and the minute digit has been found, restart if
		 * not synchronized withing the SYNCH timeout (40 m). If
		 * not, restart if the unit digit has not been found
		 * within the DATA timeout (15 m).
		 */
		if (up->status & INSYNC) {
			if (up->watch > PANIC) {
				wwv_newgame(peer);
				return;
			}
		} else if (up->status & DSYNC) {
			if (up->watch > SYNCH) {
				wwv_newgame(peer);
				return;
			}
		} else if (up->watch > DATA) {
			wwv_newgame(peer);
			return;
		}
		wwv_newchan(peer);
		break;

	/*
	 * Save the bit probability in the BCD data vector at the index
	 * given by the argument. Bits not used in the digit are forced
	 * to zero.
	 */
	case COEF1:			/* 4-7 */ 
		bcddld[arg] = bit;
		break;

	case COEF:			/* 10-13, 15-17, 20-23, 25-26,
					   30-33, 35-38, 40-41, 51-54 */
		if (up->status & DSYNC) 
			bcddld[arg] = bit;
		else
			bcddld[arg] = 0;
		break;

	case COEF2:			/* 18, 27-28, 42-43 */
		bcddld[arg] = 0;
		break;

	/*
	 * Correlate coefficient vector with each valid digit vector and
	 * save in decoding matrix. We step through the decoding matrix
	 * digits correlating each with the coefficients and saving the
	 * greatest and the next lower for later SNR calculation.
	 */
	case DECIM2:			/* 29 */
		wwv_corr4(peer, &up->decvec[arg], bcddld, bcd2);
		break;

	case DECIM3:			/* 44 */
		wwv_corr4(peer, &up->decvec[arg], bcddld, bcd3);
		break;

	case DECIM6:			/* 19 */
		wwv_corr4(peer, &up->decvec[arg], bcddld, bcd6);
		break;

	case DECIM9:			/* 8, 14, 24, 34, 39 */
		wwv_corr4(peer, &up->decvec[arg], bcddld, bcd9);
		break;

	/*
	 * Miscellaneous bits. If above the positive threshold, declare
	 * 1; if below the negative threshold, declare 0; otherwise
	 * raise the BGATE bit. The design is intended to avoid
	 * integrating noise under low SNR conditions.
	 */
	case MSC20:			/* 55 */
		wwv_corr4(peer, &up->decvec[YR + 1], bcddld, bcd9);
		/* fall through */

	case MSCBIT:			/* 2-3, 50, 56-57 */
		if (bitvec[nsec] > BTHR) {
			if (!(up->misc & arg))
				up->alarm |= CMPERR;
			up->misc |= arg;
		} else if (bitvec[nsec] < -BTHR) {
			if (up->misc & arg)
				up->alarm |= CMPERR;
			up->misc &= ~arg;
		} else {
			up->status |= BGATE;
		}
		break;

	/*
	 * Save the data channel gain, then QSY to the probe channel and
	 * dim the seconds comb filters. The www_newchan() routine will
	 * light them back up.
	 */
	case MSC21:			/* 58 */
		if (bitvec[nsec] > BTHR) {
			if (!(up->misc & arg))
				up->alarm |= CMPERR;
			up->misc |= arg;
		} else if (bitvec[nsec] < -BTHR) {
			if (up->misc & arg)
				up->alarm |= CMPERR;
			up->misc &= ~arg;
		} else {
			up->status |= BGATE;
		}
		up->status &= ~(SELV | SELH);
#ifdef ICOM
		if (up->fd_icom > 0) {
			up->schan = (up->schan + 1) % NCHAN;
			wwv_qsy(peer, up->schan);
		} else {
			up->mitig[up->achan].gain = up->gain;
		}
#else
		up->mitig[up->achan].gain = up->gain;
#endif /* ICOM */
		break;

	/*
	 * The endgames
	 *
	 * During second 59 the receiver and codec AGC are settling
	 * down, so the data pulse is unusable as quality metric. If
	 * LEPSEC is set on the last minute of 30 June or 31 December,
	 * the transmitter and receiver insert an extra second (60) in
	 * the timescale and the minute sync repeats the second. Once
	 * leaps occurred at intervals of about 18 months, but the last
	 * leap before the most recent leap in 1995 was in  1998.
	 */
	case MIN1:			/* 59 */
		if (up->status & LEPSEC)
			break;

		/* fall through */

	case MIN2:			/* 60 */
		up->status &= ~LEPSEC;
		wwv_tsec(peer);
		up->rsec = 0;
		wwv_clock(peer);
		break;
	}
	if ((pp->sloppyclockflag & CLK_FLAG4) && !(up->status &
	    DSYNC)) {
		snprintf(tbuf, sizeof(tbuf),
		    "wwv3 %2d %04x %3d %4d %5.0f %5.1f %5.0f %5.1f %5.0f",
		    nsec, up->status, up->gain, up->yepoch, up->epomax,
		    up->eposnr, up->datsig, up->datsnr, bit);
		record_clock_stats(&peer->srcadr, tbuf);
#ifdef DEBUG
		if (debug)
			printf("%s\n", tbuf);
#endif /* DEBUG */
	}
	pp->disp += AUDIO_PHI;
}

/*
 * The radio clock is set if the alarm bits are all zero. After that,
 * the time is considered valid if the second sync bit is lit. It should
 * not be a surprise, especially if the radio is not tunable, that
 * sometimes no stations are above the noise and the integrators
 * discharge below the thresholds. We assume that, after a day of signal
 * loss, the minute sync epoch will be in the same second. This requires
 * the codec frequency be accurate within 6 PPM. Practical experience
 * shows the frequency typically within 0.1 PPM, so after a day of
 * signal loss, the time should be within 8.6 ms.. 
 */
static void
wwv_clock(
	struct peer *peer	/* peer unit pointer */
	)
{
	struct refclockproc *pp;
	struct wwvunit *up;
	l_fp	offset;		/* offset in NTP seconds */

	pp = peer->procptr;
	up = pp->unitptr;
	if (!(up->status & SSYNC))
		up->alarm |= SYNERR;
	if (up->digcnt < 9)
		up->alarm |= NINERR;
	if (!(up->alarm))
		up->status |= INSYNC;
	if (up->status & INSYNC && up->status & SSYNC) {
		if (up->misc & SECWAR)
			pp->leap = LEAP_ADDSECOND;
		else
			pp->leap = LEAP_NOWARNING;
		pp->second = up->rsec;
		pp->minute = up->decvec[MN].digit + up->decvec[MN +
		    1].digit * 10;
		pp->hour = up->decvec[HR].digit + up->decvec[HR +
		    1].digit * 10;
		pp->day = up->decvec[DA].digit + up->decvec[DA +
		    1].digit * 10 + up->decvec[DA + 2].digit * 100;
		pp->year = up->decvec[YR].digit + up->decvec[YR +
		    1].digit * 10;
		pp->year += 2000;
		L_CLR(&offset);
		if (!clocktime(pp->day, pp->hour, pp->minute,
		    pp->second, GMT, up->timestamp.l_ui,
		    &pp->yearstart, &offset.l_ui)) {
			up->errflg = CEVNT_BADTIME;
		} else {
			up->watch = 0;
			pp->disp = 0;
			pp->lastref = up->timestamp;
			refclock_process_offset(pp, offset,
			    up->timestamp, PDELAY + up->pdelay);
			refclock_receive(peer);
		}
	}
	pp->lencode = timecode(up, pp->a_lastcode,
			       sizeof(pp->a_lastcode));
	record_clock_stats(&peer->srcadr, pp->a_lastcode);
#ifdef DEBUG
	if (debug)
		printf("wwv: timecode %d %s\n", pp->lencode,
		    pp->a_lastcode);
#endif /* DEBUG */
}


/*
 * wwv_corr4 - determine maximum-likelihood digit
 *
 * This routine correlates the received digit vector with the BCD
 * coefficient vectors corresponding to all valid digits at the given
 * position in the decoding matrix. The maximum value corresponds to the
 * maximum-likelihood digit, while the ratio of this value to the next
 * lower value determines the likelihood function. Note that, if the
 * digit is invalid, the likelihood vector is averaged toward a miss.
 */
static void
wwv_corr4(
	struct peer *peer,	/* peer unit pointer */
	struct decvec *vp,	/* decoding table pointer */
	double	data[],		/* received data vector */
	double	tab[][4]	/* correlation vector array */
	)
{
	struct refclockproc *pp;
	struct wwvunit *up;
	double	topmax, nxtmax;	/* metrics */
	double	acc;		/* accumulator */
	char	tbuf[TBUF];	/* monitor buffer */
	int	mldigit;	/* max likelihood digit */
	int	i, j;

	pp = peer->procptr;
	up = pp->unitptr;

	/*
	 * Correlate digit vector with each BCD coefficient vector. If
	 * any BCD digit bit is bad, consider all bits a miss. Until the
	 * minute units digit has been resolved, don't to anything else.
	 * Note the SNR is calculated as the ratio of the largest
	 * likelihood value to the next largest likelihood value.
 	 */
	mldigit = 0;
	topmax = nxtmax = -MAXAMP;
	for (i = 0; tab[i][0] != 0; i++) {
		acc = 0;
		for (j = 0; j < 4; j++)
			acc += data[j] * tab[i][j];
		acc = (vp->like[i] += (acc - vp->like[i]) / TCONST);
		if (acc > topmax) {
			nxtmax = topmax;
			topmax = acc;
			mldigit = i;
		} else if (acc > nxtmax) {
			nxtmax = acc;
		}
	}
	vp->digprb = topmax;
	vp->digsnr = wwv_snr(topmax, nxtmax);

	/*
	 * The current maximum-likelihood digit is compared to the last
	 * maximum-likelihood digit. If different, the compare counter
	 * and maximum-likelihood digit are reset.  When the compare
	 * counter reaches the BCMP threshold (3), the digit is assumed
	 * correct. When the compare counter of all nine digits have
	 * reached threshold, the clock is assumed correct.
	 *
	 * Note that the clock display digit is set before the compare
	 * counter has reached threshold; however, the clock display is
	 * not considered correct until all nine clock digits have
	 * reached threshold. This is intended as eye candy, but avoids
	 * mistakes when the signal is low and the SNR is very marginal.
	 */
	if (vp->digprb < BTHR || vp->digsnr < BSNR) {
		up->status |= BGATE;
	} else {
		if (vp->digit != mldigit) {
			up->alarm |= CMPERR;
			if (vp->count > 0)
				vp->count--;
			if (vp->count == 0)
				vp->digit = mldigit;
		} else {
			if (vp->count < BCMP)
				vp->count++;
			if (vp->count == BCMP) {
				up->status |= DSYNC;
				up->digcnt++;
			}
		}
	}
	if ((pp->sloppyclockflag & CLK_FLAG4) && !(up->status &
	    INSYNC)) {
		snprintf(tbuf, sizeof(tbuf),
		    "wwv4 %2d %04x %3d %4d %5.0f %2d %d %d %d %5.0f %5.1f",
		    up->rsec - 1, up->status, up->gain, up->yepoch,
		    up->epomax, vp->radix, vp->digit, mldigit,
		    vp->count, vp->digprb, vp->digsnr);
		record_clock_stats(&peer->srcadr, tbuf);
#ifdef DEBUG
		if (debug)
			printf("%s\n", tbuf);
#endif /* DEBUG */
	}
}


/*
 * wwv_tsec - transmitter minute processing
 *
 * This routine is called at the end of the transmitter minute. It
 * implements a state machine that advances the logical clock subject to
 * the funny rules that govern the conventional clock and calendar.
 */
static void
wwv_tsec(
	struct peer *peer	/* driver structure pointer */
	)
{
	struct refclockproc *pp;
	struct wwvunit *up;
	int minute, day, isleap;
	int temp;

	pp = peer->procptr;
	up = pp->unitptr;

	/*
	 * Advance minute unit of the day. Don't propagate carries until
	 * the unit minute digit has been found.
	 */
	temp = carry(&up->decvec[MN]);	/* minute units */
	if (!(up->status & DSYNC))
		return;

	/*
	 * Propagate carries through the day.
	 */ 
	if (temp == 0)			/* carry minutes */
		temp = carry(&up->decvec[MN + 1]);
	if (temp == 0)			/* carry hours */
		temp = carry(&up->decvec[HR]);
	if (temp == 0)
		temp = carry(&up->decvec[HR + 1]);
	// XXX: Does temp have an expected value here?

	/*
	 * Decode the current minute and day. Set leap day if the
	 * timecode leap bit is set on 30 June or 31 December. Set leap
	 * minute if the last minute on leap day, but only if the clock
	 * is syncrhronized. This code fails in 2400 AD.
	 */
	minute = up->decvec[MN].digit + up->decvec[MN + 1].digit *
	    10 + up->decvec[HR].digit * 60 + up->decvec[HR +
	    1].digit * 600;
	day = up->decvec[DA].digit + up->decvec[DA + 1].digit * 10 +
	    up->decvec[DA + 2].digit * 100;

	/*
	 * Set the leap bit on the last minute of the leap day.
	 */
	isleap = up->decvec[YR].digit & 0x3;
	if (up->misc & SECWAR && up->status & INSYNC) {
		if ((day == (isleap ? 182 : 183) || day == (isleap ?
		    365 : 366)) && minute == 1439)
			up->status |= LEPSEC;
	}

	/*
	 * Roll the day if this the first minute and propagate carries
	 * through the year.
	 */
	if (minute != 1440)
		return;

	// minute = 0;
	while (carry(&up->decvec[HR]) != 0); /* advance to minute 0 */
	while (carry(&up->decvec[HR + 1]) != 0);
	day++;
	temp = carry(&up->decvec[DA]);	/* carry days */
	if (temp == 0)
		temp = carry(&up->decvec[DA + 1]);
	if (temp == 0)
		temp = carry(&up->decvec[DA + 2]);
	// XXX: Is there an expected value of temp here?

	/*
	 * Roll the year if this the first day and propagate carries
	 * through the century.
	 */
	if (day != (isleap ? 365 : 366))
		return;

	// day = 1;
	while (carry(&up->decvec[DA]) != 1); /* advance to day 1 */
	while (carry(&up->decvec[DA + 1]) != 0);
	while (carry(&up->decvec[DA + 2]) != 0);
	temp = carry(&up->decvec[YR]);	/* carry years */
	if (temp == 0)
		carry(&up->decvec[YR + 1]);
}


/*
 * carry - process digit
 *
 * This routine rotates a likelihood vector one position and increments
 * the clock digit modulo the radix. It returns the new clock digit or
 * zero if a carry occurred. Once synchronized, the clock digit will
 * match the maximum-likelihood digit corresponding to that position.
 */
static int
carry(
	struct decvec *dp	/* decoding table pointer */
	)
{
	int temp;
	int j;

	dp->digit++;
	if (dp->digit == dp->radix)
		dp->digit = 0;
	temp = dp->like[dp->radix - 1];
	for (j = dp->radix - 1; j > 0; j--)
		dp->like[j] = dp->like[j - 1];
	dp->like[0] = temp;
	return (dp->digit);
}


/*
 * wwv_snr - compute SNR or likelihood function
 */
static double
wwv_snr(
	double signal,		/* signal */
	double noise		/* noise */
	)
{
	double rval;

	/*
	 * This is a little tricky. Due to the way things are measured,
	 * either or both the signal or noise amplitude can be negative
	 * or zero. The intent is that, if the signal is negative or
	 * zero, the SNR must always be zero. This can happen with the
	 * subcarrier SNR before the phase has been aligned. On the
	 * other hand, in the likelihood function the "noise" is the
	 * next maximum down from the peak and this could be negative.
	 * However, in this case the SNR is truly stupendous, so we
	 * simply cap at MAXSNR dB (40).
	 */
	if (signal <= 0) {
		rval = 0;
	} else if (noise <= 0) {
		rval = MAXSNR;
	} else {
		rval = 20. * log10(signal / noise);
		if (rval > MAXSNR)
			rval = MAXSNR;
	}
	return (rval);
}


/*
 * wwv_newchan - change to new data channel
 *
 * The radio actually appears to have ten channels, one channel for each
 * of five frequencies and each of two stations (WWV and WWVH), although
 * if not tunable only the DCHAN channel appears live. While the radio
 * is tuned to the working data channel frequency and station for most
 * of the minute, during seconds 59, 0 and 1 the radio is tuned to a
 * probe frequency in order to search for minute sync pulse and data
 * subcarrier from other transmitters.
 *
 * The search for WWV and WWVH operates simultaneously, with WWV minute
 * sync pulse at 1000 Hz and WWVH at 1200 Hz. The probe frequency
 * rotates each minute over 2.5, 5, 10, 15 and 20 MHz in order and yes,
 * we all know WWVH is dark on 20 MHz, but few remember when WWV was lit
 * on 25 MHz.
 *
 * This routine selects the best channel using a metric computed from
 * the reachability register and minute pulse amplitude. Normally, the
 * award goes to the the channel with the highest metric; but, in case
 * of ties, the award goes to the channel with the highest minute sync
 * pulse amplitude and then to the highest frequency.
 *
 * The routine performs an important squelch function to keep dirty data
 * from polluting the integrators. In order to consider a station valid,
 * the metric must be at least MTHR (13); otherwise, the station select
 * bits are cleared so the second sync is disabled and the data bit
 * integrators averaged to a miss. 
 */
static int
wwv_newchan(
	struct peer *peer	/* peer structure pointer */
	)
{
	struct refclockproc *pp;
	struct wwvunit *up;
	struct sync *sp, *rp;
	double rank, dtemp;
	int i, j, rval;

	pp = peer->procptr;
	up = pp->unitptr;

	/*
	 * Search all five station pairs looking for the channel with
	 * maximum metric.
	 */
	sp = NULL;
	j = 0;
	rank = 0;
	for (i = 0; i < NCHAN; i++) {
		rp = &up->mitig[i].wwvh;
		dtemp = rp->metric;
		if (dtemp >= rank) {
			rank = dtemp;
			sp = rp;
			j = i;
		}
		rp = &up->mitig[i].wwv;
		dtemp = rp->metric;
		if (dtemp >= rank) {
			rank = dtemp;
			sp = rp;
			j = i;
		}
	}

	/*
	 * If the strongest signal is less than the MTHR threshold (13),
	 * we are beneath the waves, so squelch the second sync and
	 * advance to the next station. This makes sure all stations are
	 * scanned when the ions grow dim. If the strongest signal is
	 * greater than the threshold, tune to that frequency and
	 * transmitter QTH.
	 */
	up->status &= ~(SELV | SELH);
	if (rank < MTHR) {
		up->dchan = (up->dchan + 1) % NCHAN;
		if (up->status & METRIC) {
			up->status &= ~METRIC;
			refclock_report(peer, CEVNT_PROP);
		}
		rval = FALSE;
	} else {
		up->dchan = j;
		up->sptr = sp;
		memcpy(&pp->refid, sp->refid, 4);
		peer->refid = pp->refid;
		up->status |= METRIC;
		if (sp->select & SELV) {
			up->status |= SELV;
			up->pdelay = pp->fudgetime1;
		} else if (sp->select & SELH) {
			up->status |= SELH;
			up->pdelay = pp->fudgetime2;
		} else {
			up->pdelay = 0;
		}
		rval = TRUE;
	}
#ifdef ICOM
	if (up->fd_icom > 0)
		wwv_qsy(peer, up->dchan);
#endif /* ICOM */
	return (rval);
}


/*
 * wwv_newgame - reset and start over
 *
 * There are three conditions resulting in a new game:
 *
 * 1	After finding the minute pulse (MSYNC lit), going 15 minutes
 *	(DATA) without finding the unit seconds digit.
 *
 * 2	After finding good data (DSYNC lit), going more than 40 minutes
 *	(SYNCH) without finding station sync (INSYNC lit).
 *
 * 3	After finding station sync (INSYNC lit), going more than 2 days
 *	(PANIC) without finding any station. 
 */
static void
wwv_newgame(
	struct peer *peer	/* peer structure pointer */
	)
{
	struct refclockproc *pp;
	struct wwvunit *up;
	struct chan *cp;
	int i;

	pp = peer->procptr;
	up = pp->unitptr;

	/*
	 * Initialize strategic values. Note we set the leap bits
	 * NOTINSYNC and the refid "NONE".
	 */
	if (up->status)
		up->errflg = CEVNT_TIMEOUT;
	peer->leap = LEAP_NOTINSYNC;
	up->watch = up->status = up->alarm = 0;
	up->avgint = MINAVG;
	up->freq = 0;
	up->gain = MAXGAIN / 2;

	/*
	 * Initialize the station processes for audio gain, select bit,
	 * station/frequency identifier and reference identifier. Start
	 * probing at the strongest channel or the default channel if
	 * nothing heard.
	 */
	memset(up->mitig, 0, sizeof(up->mitig));
	for (i = 0; i < NCHAN; i++) {
		cp = &up->mitig[i];
		cp->gain = up->gain;
		cp->wwv.select = SELV;
		snprintf(cp->wwv.refid, sizeof(cp->wwv.refid), "WV%.0f",
		    floor(qsy[i])); 
		cp->wwvh.select = SELH;
		snprintf(cp->wwvh.refid, sizeof(cp->wwvh.refid), "WH%.0f",
		    floor(qsy[i])); 
	}
	up->dchan = (DCHAN + NCHAN - 1) % NCHAN;
	wwv_newchan(peer);
	up->schan = up->dchan;
}

/*
 * wwv_metric - compute station metric
 *
 * The most significant bits represent the number of ones in the
 * station reachability register. The least significant bits represent
 * the minute sync pulse amplitude. The combined value is scaled 0-100.
 */
double
wwv_metric(
	struct sync *sp		/* station pointer */
	)
{
	double	dtemp;

	dtemp = sp->count * MAXAMP;
	if (sp->synmax < MAXAMP)
		dtemp += sp->synmax;
	else
		dtemp += MAXAMP - 1;
	dtemp /= (AMAX + 1) * MAXAMP;
	return (dtemp * 100.);
}


#ifdef ICOM
/*
 * wwv_qsy - Tune ICOM receiver
 *
 * This routine saves the AGC for the current channel, switches to a new
 * channel and restores the AGC for that channel. If a tunable receiver
 * is not available, just fake it.
 */
static int
wwv_qsy(
	struct peer *peer,	/* peer structure pointer */
	int	chan		/* channel */
	)
{
	int rval = 0;
	struct refclockproc *pp;
	struct wwvunit *up;

	pp = peer->procptr;
	up = pp->unitptr;
	if (up->fd_icom > 0) {
		up->mitig[up->achan].gain = up->gain;
		rval = icom_freq(up->fd_icom, peer->ttl & 0x7f,
		    qsy[chan]);
		up->achan = chan;
		up->gain = up->mitig[up->achan].gain;
	}
	return (rval);
}
#endif /* ICOM */


/*
 * timecode - assemble timecode string and length
 *
 * Prettytime format - similar to Spectracom
 *
 * sq yy ddd hh:mm:ss ld dut lset agc iden sig errs freq avgt
 *
 * s	sync indicator ('?' or ' ')
 * q	error bits (hex 0-F)
 * yyyy	year of century
 * ddd	day of year
 * hh	hour of day
 * mm	minute of hour
 * ss	second of minute)
 * l	leap second warning (' ' or 'L')
 * d	DST state ('S', 'D', 'I', or 'O')
 * dut	DUT sign and magnitude (0.1 s)
 * lset	minutes since last clock update
 * agc	audio gain (0-255)
 * iden	reference identifier (station and frequency)
 * sig	signal quality (0-100)
 * errs	bit errors in last minute
 * freq	frequency offset (PPM)
 * avgt	averaging time (s)
 */
static int
timecode(
	struct wwvunit *up,	/* driver structure pointer */
	char *		tc,	/* target string */
	size_t		tcsiz	/* target max chars */
	)
{
	struct sync *sp;
	int year, day, hour, minute, second, dut;
	char synchar, leapchar, dst;
	char cptr[50];
	

	/*
	 * Common fixed-format fields
	 */
	synchar = (up->status & INSYNC) ? ' ' : '?';
	year = up->decvec[YR].digit + up->decvec[YR + 1].digit * 10 +
	    2000;
	day = up->decvec[DA].digit + up->decvec[DA + 1].digit * 10 +
	    up->decvec[DA + 2].digit * 100;
	hour = up->decvec[HR].digit + up->decvec[HR + 1].digit * 10;
	minute = up->decvec[MN].digit + up->decvec[MN + 1].digit * 10;
	second = 0;
	leapchar = (up->misc & SECWAR) ? 'L' : ' ';
	dst = dstcod[(up->misc >> 4) & 0x3];
	dut = up->misc & 0x7;
	if (!(up->misc & DUTS))
		dut = -dut;
	snprintf(tc, tcsiz, "%c%1X", synchar, up->alarm);
	snprintf(cptr, sizeof(cptr),
		 " %4d %03d %02d:%02d:%02d %c%c %+d",
		 year, day, hour, minute, second, leapchar, dst, dut);
	strlcat(tc, cptr, tcsiz);

	/*
	 * Specific variable-format fields
	 */
	sp = up->sptr;
	snprintf(cptr, sizeof(cptr), " %d %d %s %.0f %d %.1f %d",
		 up->watch, up->mitig[up->dchan].gain, sp->refid,
		 sp->metric, up->errcnt, up->freq / WWV_SEC * 1e6,
		 up->avgint);
	strlcat(tc, cptr, tcsiz);

	return strlen(tc);
}


/*
 * wwv_gain - adjust codec gain
 *
 * This routine is called at the end of each second. During the second
 * the number of signal clips above the MAXAMP threshold (6000). If
 * there are no clips, the gain is bumped up; if there are more than
 * MAXCLP clips (100), it is bumped down. The decoder is relatively
 * insensitive to amplitude, so this crudity works just peachy. The
 * routine also jiggles the input port and selectively mutes the
 * monitor.
 */
static void
wwv_gain(
	struct peer *peer	/* peer structure pointer */
	)
{
	struct refclockproc *pp;
	struct wwvunit *up;

	pp = peer->procptr;
	up = pp->unitptr;

	/*
	 * Apparently, the codec uses only the high order bits of the
	 * gain control field. Thus, it may take awhile for changes to
	 * wiggle the hardware bits.
	 */
	if (up->clipcnt == 0) {
		up->gain += 4;
		if (up->gain > MAXGAIN)
			up->gain = MAXGAIN;
	} else if (up->clipcnt > MAXCLP) {
		up->gain -= 4;
		if (up->gain < 0)
			up->gain = 0;
	}
	audio_gain(up->gain, up->mongain, up->port);
	up->clipcnt = 0;
#if DEBUG
	if (debug > 1)
		audio_show();
#endif
}


#else
int refclock_wwv_bs;
#endif /* REFCLOCK */
