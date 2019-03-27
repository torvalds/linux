/*
 * refclock_irig - audio IRIG-B/E demodulator/decoder
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#if defined(REFCLOCK) && defined(CLOCK_IRIG)

#include "ntpd.h"
#include "ntp_io.h"
#include "ntp_refclock.h"
#include "ntp_calendar.h"
#include "ntp_stdlib.h"

#include <stdio.h>
#include <ctype.h>
#include <math.h>
#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif /* HAVE_SYS_IOCTL_H */

#include "audio.h"

/*
 * Audio IRIG-B/E demodulator/decoder
 *
 * This driver synchronizes the computer time using data encoded in
 * IRIG-B/E signals commonly produced by GPS receivers and other timing
 * devices. The IRIG signal is an amplitude-modulated carrier with
 * pulse-width modulated data bits. For IRIG-B, the carrier frequency is
 * 1000 Hz and bit rate 100 b/s; for IRIG-E, the carrier frequenchy is
 * 100 Hz and bit rate 10 b/s. The driver automatically recognizes which
 & format is in use.
 *
 * The driver requires an audio codec or sound card with sampling rate 8
 * kHz and mu-law companding. This is the same standard as used by the
 * telephone industry and is supported by most hardware and operating
 * systems, including Solaris, SunOS, FreeBSD, NetBSD and Linux. In this
 * implementation, only one audio driver and codec can be supported on a
 * single machine.
 *
 * The program processes 8000-Hz mu-law companded samples using separate
 * signal filters for IRIG-B and IRIG-E, a comb filter, envelope
 * detector and automatic threshold corrector. Cycle crossings relative
 * to the corrected slice level determine the width of each pulse and
 * its value - zero, one or position identifier.
 *
 * The data encode 20 BCD digits which determine the second, minute,
 * hour and day of the year and sometimes the year and synchronization
 * condition. The comb filter exponentially averages the corresponding
 * samples of successive baud intervals in order to reliably identify
 * the reference carrier cycle. A type-II phase-lock loop (PLL) performs
 * additional integration and interpolation to accurately determine the
 * zero crossing of that cycle, which determines the reference
 * timestamp. A pulse-width discriminator demodulates the data pulses,
 * which are then encoded as the BCD digits of the timecode.
 *
 * The timecode and reference timestamp are updated once each second
 * with IRIG-B (ten seconds with IRIG-E) and local clock offset samples
 * saved for later processing. At poll intervals of 64 s, the saved
 * samples are processed by a trimmed-mean filter and used to update the
 * system clock.
 *
 * An automatic gain control feature provides protection against
 * overdriven or underdriven input signal amplitudes. It is designed to
 * maintain adequate demodulator signal amplitude while avoiding
 * occasional noise spikes. In order to assure reliable capture, the
 * decompanded input signal amplitude must be greater than 100 units and
 * the codec sample frequency error less than 250 PPM (.025 percent).
 *
 * Monitor Data
 *
 * The timecode format used for debugging and data recording includes
 * data helpful in diagnosing problems with the IRIG signal and codec
 * connections. The driver produces one line for each timecode in the
 * following format:
 *
 * 00 00 98 23 19:26:52 2782 143 0.694 10 0.3 66.5 3094572411.00027
 *
 * If clockstats is enabled, the most recent line is written to the
 * clockstats file every 64 s. If verbose recording is enabled (fudge
 * flag 4) each line is written as generated.
 *
 * The first field containes the error flags in hex, where the hex bits
 * are interpreted as below. This is followed by the year of century,
 * day of year and time of day. Note that the time of day is for the
 * previous minute, not the current time. The status indicator and year
 * are not produced by some IRIG devices and appear as zeros. Following
 * these fields are the carrier amplitude (0-3000), codec gain (0-255),
 * modulation index (0-1), time constant (4-10), carrier phase error
 * +-.5) and carrier frequency error (PPM). The last field is the on-
 * time timestamp in NTP format.
 *
 * The error flags are defined as follows in hex:
 *
 * x01	Low signal. The carrier amplitude is less than 100 units. This
 *	is usually the result of no signal or wrong input port.
 * x02	Frequency error. The codec frequency error is greater than 250
 *	PPM. This may be due to wrong signal format or (rarely)
 *	defective codec.
 * x04	Modulation error. The IRIG modulation index is less than 0.5.
 *	This is usually the result of an overdriven codec, wrong signal
 *	format or wrong input port.
 * x08	Frame synch error. The decoder frame does not match the IRIG
 *	frame. This is usually the result of an overdriven codec, wrong
 *	signal format or noisy IRIG signal. It may also be the result of
 *	an IRIG signature check which indicates a failure of the IRIG
 *	signal synchronization source.
 * x10	Data bit error. The data bit length is out of tolerance. This is
 *	usually the result of an overdriven codec, wrong signal format
 *	or noisy IRIG signal.
 * x20	Seconds numbering discrepancy. The decoder second does not match
 *	the IRIG second. This is usually the result of an overdriven
 *	codec, wrong signal format or noisy IRIG signal.
 * x40	Codec error (overrun). The machine is not fast enough to keep up
 *	with the codec.
 * x80	Device status error (Spectracom).
 *
 *
 * Once upon a time, an UltrSPARC 30 and Solaris 2.7 kept the clock
 * within a few tens of microseconds relative to the IRIG-B signal.
 * Accuracy with IRIG-E was about ten times worse. Unfortunately, Sun
 * broke the 2.7 audio driver in 2.8, which has a 10-ms sawtooth
 * modulation.
 *
 * Unlike other drivers, which can have multiple instantiations, this
 * one supports only one. It does not seem likely that more than one
 * audio codec would be useful in a single machine. More than one would
 * probably chew up too much CPU time anyway.
 *
 * Fudge factors
 *
 * Fudge flag4 causes the dubugging output described above to be
 * recorded in the clockstats file. Fudge flag2 selects the audio input
 * port, where 0 is the mike port (default) and 1 is the line-in port.
 * It does not seem useful to select the compact disc player port. Fudge
 * flag3 enables audio monitoring of the input signal. For this purpose,
 * the monitor gain is set t a default value. Fudgetime2 is used as a
 * frequency vernier for broken codec sample frequency.
 *
 * Alarm codes
 *
 * CEVNT_BADTIME	invalid date or time
 * CEVNT_TIMEOUT	no IRIG data since last poll
 */
/*
 * Interface definitions
 */
#define	DEVICE_AUDIO	"/dev/audio" /* audio device name */
#define	PRECISION	(-17)	/* precision assumed (about 10 us) */
#define	REFID		"IRIG"	/* reference ID */
#define	DESCRIPTION	"Generic IRIG Audio Driver" /* WRU */
#define	AUDIO_BUFSIZ	320	/* audio buffer size (40 ms) */
#define SECOND		8000	/* nominal sample rate (Hz) */
#define BAUD		80	/* samples per baud interval */
#define OFFSET		128	/* companded sample offset */
#define SIZE		256	/* decompanding table size */
#define CYCLE		8	/* samples per bit */
#define SUBFLD		10	/* bits per frame */
#define FIELD		100	/* bits per second */
#define MINTC		2	/* min PLL time constant */
#define MAXTC		10	/* max PLL time constant max */
#define	MAXAMP		3000.	/* maximum signal amplitude */
#define	MINAMP		2000.	/* minimum signal amplitude */
#define DRPOUT		100.	/* dropout signal amplitude */
#define MODMIN		0.5	/* minimum modulation index */
#define MAXFREQ		(250e-6 * SECOND) /* freq tolerance (.025%) */

/*
 * The on-time synchronization point is the positive-going zero crossing
 * of the first cycle of the second. The IIR baseband filter phase delay
 * is 1.03 ms for IRIG-B and 3.47 ms for IRIG-E. The fudge value 2.68 ms
 * due to the codec and other causes was determined by calibrating to a
 * PPS signal from a GPS receiver.
 *
 * The results with a 2.4-GHz P4 running FreeBSD 6.1 are generally
 * within .02 ms short-term with .02 ms jitter. The processor load due
 * to the driver is 0.51 percent.
 */
#define IRIG_B	((1.03 + 2.68) / 1000)	/* IRIG-B system delay (s) */
#define IRIG_E	((3.47 + 2.68) / 1000)	/* IRIG-E system delay (s) */

/*
 * Data bit definitions
 */
#define BIT0		0	/* zero */
#define BIT1		1	/* one */
#define BITP		2	/* position identifier */

/*
 * Error flags
 */
#define IRIG_ERR_AMP	0x01	/* low carrier amplitude */
#define IRIG_ERR_FREQ	0x02	/* frequency tolerance exceeded */
#define IRIG_ERR_MOD	0x04	/* low modulation index */
#define IRIG_ERR_SYNCH	0x08	/* frame synch error */
#define IRIG_ERR_DECODE	0x10	/* frame decoding error */
#define IRIG_ERR_CHECK	0x20	/* second numbering discrepancy */
#define IRIG_ERR_ERROR	0x40	/* codec error (overrun) */
#define IRIG_ERR_SIGERR	0x80	/* IRIG status error (Spectracom) */

static	char	hexchar[] = "0123456789abcdef";

/*
 * IRIG unit control structure
 */
struct irigunit {
	u_char	timecode[2 * SUBFLD + 1]; /* timecode string */
	l_fp	timestamp;	/* audio sample timestamp */
	l_fp	tick;		/* audio sample increment */
	l_fp	refstamp;	/* reference timestamp */
	l_fp	chrstamp;	/* baud timestamp */
	l_fp	prvstamp;	/* previous baud timestamp */
	double	integ[BAUD];	/* baud integrator */
	double	phase, freq;	/* logical clock phase and frequency */
	double	zxing;		/* phase detector integrator */
	double	yxing;		/* cycle phase */
	double	exing;		/* envelope phase */
	double	modndx;		/* modulation index */
	double	irig_b;		/* IRIG-B signal amplitude */
	double	irig_e;		/* IRIG-E signal amplitude */
	int	errflg;		/* error flags */
	/*
	 * Audio codec variables
	 */
	double	comp[SIZE];	/* decompanding table */
	double	signal;		/* peak signal for AGC */
	int	port;		/* codec port */
	int	gain;		/* codec gain */
	int	mongain;	/* codec monitor gain */
	int	seccnt;		/* second interval counter */

	/*
	 * RF variables
	 */
	double	bpf[9];		/* IRIG-B filter shift register */
	double	lpf[5];		/* IRIG-E filter shift register */
	double	envmin, envmax;	/* envelope min and max */
	double	slice;		/* envelope slice level */
	double	intmin, intmax;	/* integrated envelope min and max */
	double	maxsignal;	/* integrated peak amplitude */
	double	noise;		/* integrated noise amplitude */
	double	lastenv[CYCLE];	/* last cycle amplitudes */
	double	lastint[CYCLE];	/* last integrated cycle amplitudes */
	double	lastsig;	/* last carrier sample */
	double	fdelay;		/* filter delay */
	int	decim;		/* sample decimation factor */
	int	envphase;	/* envelope phase */
	int	envptr;		/* envelope phase pointer */
	int	envsw;		/* envelope state */
	int	envxing;	/* envelope slice crossing */
	int	tc;		/* time constant */
	int	tcount;		/* time constant counter */
	int	badcnt;		/* decimation interval counter */

	/*
	 * Decoder variables
	 */
	int	pulse;		/* cycle counter */
	int	cycles;		/* carrier cycles */
	int	dcycles;	/* data cycles */
	int	lastbit;	/* last code element */
	int	second;		/* previous second */
	int	bitcnt;		/* bit count in frame */
	int	frmcnt;		/* bit count in second */
	int	xptr;		/* timecode pointer */
	int	bits;		/* demodulated bits */
};

/*
 * Function prototypes
 */
static	int	irig_start	(int, struct peer *);
static	void	irig_shutdown	(int, struct peer *);
static	void	irig_receive	(struct recvbuf *);
static	void	irig_poll	(int, struct peer *);

/*
 * More function prototypes
 */
static	void	irig_base	(struct peer *, double);
static	void	irig_rf		(struct peer *, double);
static	void	irig_baud	(struct peer *, int);
static	void	irig_decode	(struct peer *, int);
static	void	irig_gain	(struct peer *);

/*
 * Transfer vector
 */
struct	refclock refclock_irig = {
	irig_start,		/* start up driver */
	irig_shutdown,		/* shut down driver */
	irig_poll,		/* transmit poll message */
	noentry,		/* not used (old irig_control) */
	noentry,		/* initialize driver (not used) */
	noentry,		/* not used (old irig_buginfo) */
	NOFLAGS			/* not used */
};


/*
 * irig_start - open the devices and initialize data for processing
 */
static int
irig_start(
	int	unit,		/* instance number (used for PCM) */
	struct peer *peer	/* peer structure pointer */
	)
{
	struct refclockproc *pp;
	struct irigunit *up;

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
#endif

	/*
	 * Allocate and initialize unit structure
	 */
	up = emalloc_zero(sizeof(*up));
	pp = peer->procptr;
	pp->io.clock_recv = irig_receive;
	pp->io.srcclock = peer;
	pp->io.datalen = 0;
	pp->io.fd = fd;
	if (!io_addclock(&pp->io)) {
		close(fd);
		pp->io.fd = -1;
		free(up);
		return (0);
	}
	pp->unitptr = up;

	/*
	 * Initialize miscellaneous variables
	 */
	peer->precision = PRECISION;
	pp->clockdesc = DESCRIPTION;
	memcpy((char *)&pp->refid, REFID, 4);
	up->tc = MINTC;
	up->decim = 1;
	up->gain = 127;

	/*
	 * The companded samples are encoded sign-magnitude. The table
	 * contains all the 256 values in the interest of speed.
	 */
	up->comp[0] = up->comp[OFFSET] = 0.;
	up->comp[1] = 1; up->comp[OFFSET + 1] = -1.;
	up->comp[2] = 3; up->comp[OFFSET + 2] = -3.;
	step = 2.;
	for (i = 3; i < OFFSET; i++) {
		up->comp[i] = up->comp[i - 1] + step;
		up->comp[OFFSET + i] = -up->comp[i];
		if (i % 16 == 0)
			step *= 2.;
	}
	DTOLFP(1. / SECOND, &up->tick);
	return (1);
}


/*
 * irig_shutdown - shut down the clock
 */
static void
irig_shutdown(
	int	unit,		/* instance number (not used) */
	struct peer *peer	/* peer structure pointer */
	)
{
	struct refclockproc *pp;
	struct irigunit *up;

	pp = peer->procptr;
	up = pp->unitptr;
	if (-1 != pp->io.fd)
		io_closeclock(&pp->io);
	if (NULL != up)
		free(up);
}


/*
 * irig_receive - receive data from the audio device
 *
 * This routine reads input samples and adjusts the logical clock to
 * track the irig clock by dropping or duplicating codec samples.
 */
static void
irig_receive(
	struct recvbuf *rbufp	/* receive buffer structure pointer */
	)
{
	struct peer *peer;
	struct refclockproc *pp;
	struct irigunit *up;

	/*
	 * Local variables
	 */
	double	sample;		/* codec sample */
	u_char	*dpt;		/* buffer pointer */
	int	bufcnt;		/* buffer counter */
	l_fp	ltemp;		/* l_fp temp */

	peer = rbufp->recv_peer;
	pp = peer->procptr;
	up = pp->unitptr;

	/*
	 * Main loop - read until there ain't no more. Note codec
	 * samples are bit-inverted.
	 */
	DTOLFP((double)rbufp->recv_length / SECOND, &ltemp);
	L_SUB(&rbufp->recv_time, &ltemp);
	up->timestamp = rbufp->recv_time;
	dpt = rbufp->recv_buffer;
	for (bufcnt = 0; bufcnt < rbufp->recv_length; bufcnt++) {
		sample = up->comp[~*dpt++ & 0xff];

		/*
		 * Variable frequency oscillator. The codec oscillator
		 * runs at the nominal rate of 8000 samples per second,
		 * or 125 us per sample. A frequency change of one unit
		 * results in either duplicating or deleting one sample
		 * per second, which results in a frequency change of
		 * 125 PPM.
		 */
		up->phase += (up->freq + clock_codec) / SECOND;
		up->phase += pp->fudgetime2 / 1e6;
		if (up->phase >= .5) {
			up->phase -= 1.;
		} else if (up->phase < -.5) {
			up->phase += 1.;
			irig_rf(peer, sample);
			irig_rf(peer, sample);
		} else {
			irig_rf(peer, sample);
		}
		L_ADD(&up->timestamp, &up->tick);
		sample = fabs(sample);
		if (sample > up->signal)
			up->signal = sample;
		up->signal += (sample - up->signal) /
		    1000;

		/*
		 * Once each second, determine the IRIG format and gain.
		 */
		up->seccnt = (up->seccnt + 1) % SECOND;
		if (up->seccnt == 0) {
			if (up->irig_b > up->irig_e) {
				up->decim = 1;
				up->fdelay = IRIG_B;
			} else {
				up->decim = 10;
				up->fdelay = IRIG_E;
			}
			up->irig_b = up->irig_e = 0;
			irig_gain(peer);

		}
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
 * irig_rf - RF processing
 *
 * This routine filters the RF signal using a bandass filter for IRIG-B
 * and a lowpass filter for IRIG-E. In case of IRIG-E, the samples are
 * decimated by a factor of ten. Note that the codec filters function as
 * roofing filters to attenuate both the high and low ends of the
 * passband. IIR filter coefficients were determined using Matlab Signal
 * Processing Toolkit.
 */
static void
irig_rf(
	struct peer *peer,	/* peer structure pointer */
	double	sample		/* current signal sample */
	)
{
	struct refclockproc *pp;
	struct irigunit *up;

	/*
	 * Local variables
	 */
	double	irig_b, irig_e;	/* irig filter outputs */

	pp = peer->procptr;
	up = pp->unitptr;

	/*
	 * IRIG-B filter. Matlab 4th-order IIR elliptic, 800-1200 Hz
	 * bandpass, 0.3 dB passband ripple, -50 dB stopband ripple,
	 * phase delay 1.03 ms.
	 */
	irig_b = (up->bpf[8] = up->bpf[7]) * 6.505491e-001;
	irig_b += (up->bpf[7] = up->bpf[6]) * -3.875180e+000;
	irig_b += (up->bpf[6] = up->bpf[5]) * 1.151180e+001;
	irig_b += (up->bpf[5] = up->bpf[4]) * -2.141264e+001;
	irig_b += (up->bpf[4] = up->bpf[3]) * 2.712837e+001;
	irig_b += (up->bpf[3] = up->bpf[2]) * -2.384486e+001;
	irig_b += (up->bpf[2] = up->bpf[1]) * 1.427663e+001;
	irig_b += (up->bpf[1] = up->bpf[0]) * -5.352734e+000;
	up->bpf[0] = sample - irig_b;
	irig_b = up->bpf[0] * 4.952157e-003
	    + up->bpf[1] * -2.055878e-002
	    + up->bpf[2] * 4.401413e-002
	    + up->bpf[3] * -6.558851e-002
	    + up->bpf[4] * 7.462108e-002
	    + up->bpf[5] * -6.558851e-002
	    + up->bpf[6] * 4.401413e-002
	    + up->bpf[7] * -2.055878e-002
	    + up->bpf[8] * 4.952157e-003;
	up->irig_b += irig_b * irig_b;

	/*
	 * IRIG-E filter. Matlab 4th-order IIR elliptic, 130-Hz lowpass,
	 * 0.3 dB passband ripple, -50 dB stopband ripple, phase delay
	 * 3.47 ms.
	 */
	irig_e = (up->lpf[4] = up->lpf[3]) * 8.694604e-001;
	irig_e += (up->lpf[3] = up->lpf[2]) * -3.589893e+000;
	irig_e += (up->lpf[2] = up->lpf[1]) * 5.570154e+000;
	irig_e += (up->lpf[1] = up->lpf[0]) * -3.849667e+000;
	up->lpf[0] = sample - irig_e;
	irig_e = up->lpf[0] * 3.215696e-003
	    + up->lpf[1] * -1.174951e-002
	    + up->lpf[2] * 1.712074e-002
	    + up->lpf[3] * -1.174951e-002
	    + up->lpf[4] * 3.215696e-003;
	up->irig_e += irig_e * irig_e;

	/*
	 * Decimate by a factor of either 1 (IRIG-B) or 10 (IRIG-E).
	 */
	up->badcnt = (up->badcnt + 1) % up->decim;
	if (up->badcnt == 0) {
		if (up->decim == 1)
			irig_base(peer, irig_b);
		else
			irig_base(peer, irig_e);
	}
}

/*
 * irig_base - baseband processing
 *
 * This routine processes the baseband signal and demodulates the AM
 * carrier using a synchronous detector. It then synchronizes to the
 * data frame at the baud rate and decodes the width-modulated data
 * pulses.
 */
static void
irig_base(
	struct peer *peer,	/* peer structure pointer */
	double	sample		/* current signal sample */
	)
{
	struct refclockproc *pp;
	struct irigunit *up;

	/*
	 * Local variables
	 */
	double	lope;		/* integrator output */
	double	env;		/* envelope detector output */
	double	dtemp;
	int	carphase;	/* carrier phase */

	pp = peer->procptr;
	up = pp->unitptr;

	/*
	 * Synchronous baud integrator. Corresponding samples of current
	 * and past baud intervals are integrated to refine the envelope
	 * amplitude and phase estimate. We keep one cycle (1 ms) of the
	 * raw data and one baud (10 ms) of the integrated data.
	 */
	up->envphase = (up->envphase + 1) % BAUD;
	up->integ[up->envphase] += (sample - up->integ[up->envphase]) /
	    (5 * up->tc);
	lope = up->integ[up->envphase];
	carphase = up->envphase % CYCLE;
	up->lastenv[carphase] = sample;
	up->lastint[carphase] = lope;

	/*
	 * Phase detector. Find the negative-going zero crossing
	 * relative to sample 4 in the 8-sample sycle. A phase change of
	 * 360 degrees produces an output change of one unit.
	 */ 
	if (up->lastsig > 0 && lope <= 0)
		up->zxing += (double)(carphase - 4) / CYCLE;
	up->lastsig = lope;

	/*
	 * End of the baud. Update signal/noise estimates and PLL
	 * phase, frequency and time constant.
	 */
	if (up->envphase == 0) {
		up->maxsignal = up->intmax; up->noise = up->intmin;
		up->intmin = 1e6; up->intmax = -1e6;
		if (up->maxsignal < DRPOUT)
			up->errflg |= IRIG_ERR_AMP;
		if (up->maxsignal > 0)
			up->modndx = (up->maxsignal - up->noise) /
			    up->maxsignal;
 		else
			up->modndx = 0;
		if (up->modndx < MODMIN)
			up->errflg |= IRIG_ERR_MOD;
		if (up->errflg & (IRIG_ERR_AMP | IRIG_ERR_FREQ |
		   IRIG_ERR_MOD | IRIG_ERR_SYNCH)) {
			up->tc = MINTC;
			up->tcount = 0;
		}

		/*
		 * Update PLL phase and frequency. The PLL time constant
		 * is set initially to stabilize the frequency within a
		 * minute or two, then increases to the maximum. The
		 * frequency is clamped so that the PLL capture range
		 * cannot be exceeded.
		 */
		dtemp = up->zxing * up->decim / BAUD;
		up->yxing = dtemp;
		up->zxing = 0.;
		up->phase += dtemp / up->tc;
		up->freq += dtemp / (4. * up->tc * up->tc);
		if (up->freq > MAXFREQ) {
			up->freq = MAXFREQ;
			up->errflg |= IRIG_ERR_FREQ;
		} else if (up->freq < -MAXFREQ) {
			up->freq = -MAXFREQ;
			up->errflg |= IRIG_ERR_FREQ;
		}
	}

	/*
	 * Synchronous demodulator. There are eight samples in the cycle
	 * and ten cycles in the baud. Since the PLL has aligned the
	 * negative-going zero crossing at sample 4, the maximum
	 * amplitude is at sample 2 and minimum at sample 6. The
	 * beginning of the data pulse is determined from the integrated
	 * samples, while the end of the pulse is determined from the
	 * raw samples. The raw data bits are demodulated relative to
	 * the slice level and left-shifted in the decoding register.
	 */
	if (carphase != 7)
		return;

	lope = (up->lastint[2] - up->lastint[6]) / 2.;
	if (lope > up->intmax)
		up->intmax = lope;
	if (lope < up->intmin)
		up->intmin = lope;

	/*
	 * Pulse code demodulator and reference timestamp. The decoder
	 * looks for a sequence of ten bits; the first two bits must be
	 * one, the last two bits must be zero. Frame synch is asserted
	 * when three correct frames have been found.
	 */
	up->pulse = (up->pulse + 1) % 10;
	up->cycles <<= 1;
	if (lope >= (up->maxsignal + up->noise) / 2.)
		up->cycles |= 1;
	if ((up->cycles & 0x303c0f03) == 0x300c0300) {
		if (up->pulse != 0)
			up->errflg |= IRIG_ERR_SYNCH;
		up->pulse = 0;
	}

	/*
	 * Assemble the baud and max/min to get the slice level for the
	 * next baud. The slice level is based on the maximum over the
	 * first two bits and the minimum over the last two bits, with
	 * the slice level halfway between the maximum and minimum.
	 */
	env = (up->lastenv[2] - up->lastenv[6]) / 2.;
	up->dcycles <<= 1;
	if (env >= up->slice)
		up->dcycles |= 1;
	switch(up->pulse) {

	case 0:
		irig_baud(peer, up->dcycles);
		if (env < up->envmin)
			up->envmin = env;
		up->slice = (up->envmax + up->envmin) / 2;
		up->envmin = 1e6; up->envmax = -1e6;
		break;

	case 1:
		up->envmax = env;
		break;

	case 2:
		if (env > up->envmax)
			up->envmax = env;
		break;

	case 9:
		up->envmin = env;
		break;
	}
}

/*
 * irig_baud - update the PLL and decode the pulse-width signal
 */
static void
irig_baud(
	struct peer *peer,	/* peer structure pointer */
	int	bits		/* decoded bits */
	)
{
	struct refclockproc *pp;
	struct irigunit *up;
	double	dtemp;
	l_fp	ltemp;

        pp = peer->procptr;
	up = pp->unitptr;

	/*
	 * The PLL time constant starts out small, in order to
	 * sustain a frequency tolerance of 250 PPM. It
	 * gradually increases as the loop settles down. Note
	 * that small wiggles are not believed, unless they
	 * persist for lots of samples.
	 */
	up->exing = -up->yxing;
	if (abs(up->envxing - up->envphase) <= 1) {
		up->tcount++;
		if (up->tcount > 20 * up->tc) {
			up->tc++;
			if (up->tc > MAXTC)
				up->tc = MAXTC;
			up->tcount = 0;
			up->envxing = up->envphase;
		} else {
			up->exing -= up->envxing - up->envphase;
		}
	} else {
		up->tcount = 0;
		up->envxing = up->envphase;
	}

	/*
	 * Strike the baud timestamp as the positive zero crossing of
	 * the first bit, accounting for the codec delay and filter
	 * delay.
	 */
	up->prvstamp = up->chrstamp;
	dtemp = up->decim * (up->exing / SECOND) + up->fdelay;
	DTOLFP(dtemp, &ltemp);
	up->chrstamp = up->timestamp;
	L_SUB(&up->chrstamp, &ltemp);

	/*
	 * The data bits are collected in ten-bit bauds. The first two
	 * bits are not used. The resulting patterns represent runs of
	 * 0-1 bits (0), 2-4 bits (1) and 5-7 bits (PI). The remaining
	 * 8-bit run represents a soft error and is treated as 0.
	 */
	switch (up->dcycles & 0xff) {

	case 0x00:		/* 0-1 bits (0) */
	case 0x80:
		irig_decode(peer, BIT0);
		break;

	case 0xc0:		/* 2-4 bits (1) */
	case 0xe0:
	case 0xf0:
		irig_decode(peer, BIT1);
		break;

	case 0xf8:		/* (5-7 bits (PI) */
	case 0xfc:
	case 0xfe:
		irig_decode(peer, BITP);
		break;

	default:		/* 8 bits (error) */
		irig_decode(peer, BIT0);
		up->errflg |= IRIG_ERR_DECODE;
	}
}


/*
 * irig_decode - decode the data
 *
 * This routine assembles bauds into digits, digits into frames and
 * frames into the timecode fields. Bits can have values of zero, one
 * or position identifier. There are four bits per digit, ten digits per
 * frame and ten frames per second.
 */
static void
irig_decode(
	struct	peer *peer,	/* peer structure pointer */
	int	bit		/* data bit (0, 1 or 2) */
	)
{
	struct refclockproc *pp;
	struct irigunit *up;

	/*
	 * Local variables
	 */
	int	syncdig;	/* sync digit (Spectracom) */
	char	sbs[6 + 1];	/* binary seconds since 0h */
	char	spare[2 + 1];	/* mulligan digits */
	int	temp;

	syncdig = 0;
	pp = peer->procptr;
	up = pp->unitptr;

	/*
	 * Assemble frame bits.
	 */
	up->bits >>= 1;
	if (bit == BIT1) {
		up->bits |= 0x200;
	} else if (bit == BITP && up->lastbit == BITP) {

		/*
		 * Frame sync - two adjacent position identifiers, which
		 * mark the beginning of the second. The reference time
		 * is the beginning of the second position identifier,
		 * so copy the character timestamp to the reference
		 * timestamp.
		 */
		if (up->frmcnt != 1)
			up->errflg |= IRIG_ERR_SYNCH;
		up->frmcnt = 1;
		up->refstamp = up->prvstamp;
	}
	up->lastbit = bit;
	if (up->frmcnt % SUBFLD == 0) {

		/*
		 * End of frame. Encode two hexadecimal digits in
		 * little-endian timecode field. Note frame 1 is shifted
		 * right one bit to account for the marker PI.
		 */
		temp = up->bits;
		if (up->frmcnt == 10)
			temp >>= 1;
		if (up->xptr >= 2) {
			up->timecode[--up->xptr] = hexchar[temp & 0xf];
			up->timecode[--up->xptr] = hexchar[(temp >> 5) &
			    0xf];
		}
		if (up->frmcnt == 0) {

			/*
			 * End of second. Decode the timecode and wind
			 * the clock. Not all IRIG generators have the
			 * year; if so, it is nonzero after year 2000.
			 * Not all have the hardware status bit; if so,
			 * it is lit when the source is okay and dim
			 * when bad. We watch this only if the year is
			 * nonzero. Not all are configured for signature
			 * control. If so, all BCD digits are set to
			 * zero if the source is bad. In this case the
			 * refclock_process() will reject the timecode
			 * as invalid.
			 */
			up->xptr = 2 * SUBFLD;
			if (sscanf((char *)up->timecode,
			   "%6s%2d%1d%2s%3d%2d%2d%2d", sbs, &pp->year,
			    &syncdig, spare, &pp->day, &pp->hour,
			    &pp->minute, &pp->second) != 8)
				pp->leap = LEAP_NOTINSYNC;
			else
				pp->leap = LEAP_NOWARNING;
			up->second = (up->second + up->decim) % 60;

			/*
			 * Raise an alarm if the day field is zero,
			 * which happens when signature control is
			 * enabled and the device has lost
			 * synchronization. Raise an alarm if the year
			 * field is nonzero and the sync indicator is
			 * zero, which happens when a Spectracom radio
			 * has lost synchronization. Raise an alarm if
			 * the expected second does not agree with the
			 * decoded second, which happens with a garbled
			 * IRIG signal. We are very particular.
			 */
			if (pp->day == 0 || (pp->year != 0 && syncdig ==
			    0))
				up->errflg |= IRIG_ERR_SIGERR;
			if (pp->second != up->second)
				up->errflg |= IRIG_ERR_CHECK;
			up->second = pp->second;

			/*
			 * Wind the clock only if there are no errors
			 * and the time constant has reached the
			 * maximum.
			 */
			if (up->errflg == 0 && up->tc == MAXTC) {
				pp->lastref = pp->lastrec;
				pp->lastrec = up->refstamp;
				if (!refclock_process(pp))
					refclock_report(peer,
					    CEVNT_BADTIME);
			}
			snprintf(pp->a_lastcode, sizeof(pp->a_lastcode),
			    "%02x %02d %03d %02d:%02d:%02d %4.0f %3d %6.3f %2d %6.2f %6.1f %s",
			    up->errflg, pp->year, pp->day,
			    pp->hour, pp->minute, pp->second,
			    up->maxsignal, up->gain, up->modndx,
			    up->tc, up->exing * 1e6 / SECOND, up->freq *
			    1e6 / SECOND, ulfptoa(&pp->lastrec, 6));
			pp->lencode = strlen(pp->a_lastcode);
			up->errflg = 0;
			if (pp->sloppyclockflag & CLK_FLAG4) {
				record_clock_stats(&peer->srcadr,
				    pp->a_lastcode);
#ifdef DEBUG
				if (debug)
					printf("irig %s\n",
					    pp->a_lastcode);
#endif /* DEBUG */
			}
		}
	}
	up->frmcnt = (up->frmcnt + 1) % FIELD;
}


/*
 * irig_poll - called by the transmit procedure
 *
 * This routine sweeps up the timecode updates since the last poll. For
 * IRIG-B there should be at least 60 updates; for IRIG-E there should
 * be at least 6. If nothing is heard, a timeout event is declared. 
 */
static void
irig_poll(
	int	unit,		/* instance number (not used) */
	struct peer *peer	/* peer structure pointer */
	)
{
	struct refclockproc *pp;

	pp = peer->procptr;

	if (pp->coderecv == pp->codeproc) {
		refclock_report(peer, CEVNT_TIMEOUT);
		return;

	}
	refclock_receive(peer);
	if (!(pp->sloppyclockflag & CLK_FLAG4)) {
		record_clock_stats(&peer->srcadr, pp->a_lastcode);
#ifdef DEBUG
		if (debug)
			printf("irig %s\n", pp->a_lastcode);
#endif /* DEBUG */
	}
	pp->polls++;
	
}


/*
 * irig_gain - adjust codec gain
 *
 * This routine is called at the end of each second. It uses the AGC to
 * bradket the maximum signal level between MINAMP and MAXAMP to avoid
 * hunting. The routine also jiggles the input port and selectively
 * mutes the monitor.
 */
static void
irig_gain(
	struct peer *peer	/* peer structure pointer */
	)
{
	struct refclockproc *pp;
	struct irigunit *up;

	pp = peer->procptr;
	up = pp->unitptr;

	/*
	 * Apparently, the codec uses only the high order bits of the
	 * gain control field. Thus, it may take awhile for changes to
	 * wiggle the hardware bits.
	 */
	if (up->maxsignal < MINAMP) {
		up->gain += 4;
		if (up->gain > MAXGAIN)
			up->gain = MAXGAIN;
	} else if (up->maxsignal > MAXAMP) {
		up->gain -= 4;
		if (up->gain < 0)
			up->gain = 0;
	}
	audio_gain(up->gain, up->mongain, up->port);
}


#else
int refclock_irig_bs;
#endif /* REFCLOCK */
