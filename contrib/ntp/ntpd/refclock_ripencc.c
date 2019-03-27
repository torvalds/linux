/*
 * $Id: refclock_ripencc.c,v 1.13 2002/06/18 14:20:55 marks Exp marks $
 *
 * Copyright (c) 2002  RIPE NCC
 *
 * All Rights Reserved
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose and without fee is hereby granted,
 * provided that the above copyright notice appear in all copies and that
 * both that copyright notice and this permission notice appear in
 * supporting documentation, and that the name of the author not be
 * used in advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.
 *
 * THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
 * ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS; IN NO EVENT SHALL
 * AUTHOR BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY
 * DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 *
 *
 * This driver was developed for use with the RIPE NCC TTM project.
 *
 *
 * The initial driver was developed by Daniel Karrenberg <dfk@ripe.net> 
 * using the code made available by Trimble. This was for xntpd-3.x.x
 *
 * Rewrite of the driver for ntpd-4.x.x by Mark Santcroos <marks@ripe.net>
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#if defined(REFCLOCK) && defined(CLOCK_RIPENCC)

#include "ntp_stdlib.h"
#include "ntpd.h"
#include "ntp_refclock.h"
#include "ntp_unixtime.h"
#include "ntp_io.h"

#ifdef HAVE_PPSAPI
# include "ppsapi_timepps.h"
#endif

/*
 * Definitions
 */

/* we are on little endian */
#define BYTESWAP

/* 
 * DEBUG statements: uncomment if necessary
 */
/* #define DEBUG_NCC */ /* general debug statements */
/* #define DEBUG_PPS */ /* debug pps */
/* #define DEBUG_RAW */ /* print raw packets */

#define TRIMBLE_OUTPUT_FUNC
#define TSIP_VERNUM "7.12a"

#ifndef FALSE
#define FALSE 	(0)
#define TRUE 	(!FALSE)
#endif /* FALSE */

#define GPS_PI 	(3.1415926535898)
#define GPS_C 		(299792458.)
#define	D2R		(GPS_PI/180.0)
#define	R2D		(180.0/GPS_PI)
#define WEEK 	(604800.)
#define MAXCHAN  (8)

/* control characters for TSIP packets */
#define DLE 	(0x10)
#define ETX 	(0x03)

#define MAX_RPTBUF (256)

/* values of TSIPPKT.status */
#define TSIP_PARSED_EMPTY 	0
#define TSIP_PARSED_FULL 	1
#define TSIP_PARSED_DLE_1 	2
#define TSIP_PARSED_DATA 	3
#define TSIP_PARSED_DLE_2 	4

#define UTCF_UTC_AVAIL  (unsigned char) (1)     /* UTC available */
#define UTCF_LEAP_SCHD  (unsigned char) (1<<4)  /* Leap scheduled */
#define UTCF_LEAP_PNDG  (unsigned char) (1<<5)  /* Leap pending, will occur at end of day */

#define DEVICE  "/dev/gps%d"	/* name of radio device */
#define PRECISION       (-9)    /* precision assumed (about 2 ms) */
#define PPS_PRECISION   (-20)	/* precision assumed (about 1 us) */
#define REFID           "GPS\0" /* reference id */
#define REFID_LEN	4
#define DESCRIPTION     "RIPE NCC GPS (Palisade)"	/* Description */
#define SPEED232        B9600   /* 9600 baud */

#define NSAMPLES        3       /* stages of median filter */

/* Structures */

/* TSIP packets have the following structure, whether report or command. */
typedef struct {
	short 
	    counter,		/* counter */
	    len;		/* size of buf; < MAX_RPTBUF unsigned chars */
	unsigned char
	    status,		/* TSIP packet format/parse status */
	    code,		/* TSIP code */
	    buf[MAX_RPTBUF];	/* report or command string */
} TSIPPKT;

/* TSIP binary data structures */
typedef struct {
	unsigned char
	    t_oa_raw, SV_health;
	float
	    e, t_oa, i_0, OMEGADOT, sqrt_A,
	    OMEGA_0, omega, M_0, a_f0, a_f1,
	    Axis, n, OMEGA_n, ODOT_n, t_zc;
	short
	    weeknum, wn_oa;
} ALM_INFO;

typedef struct {		/*  Almanac health page (25) parameters  */
	unsigned char
	    WN_a, SV_health[32], t_oa;
} ALH_PARMS;

typedef struct {		/*  Universal Coordinated Time (UTC) parms */
	double
	    A_0;
	float
	    A_1;
	short
	    delta_t_LS;
	float
	    t_ot;
	short
	    WN_t, WN_LSF, DN, delta_t_LSF;
} UTC_INFO;

typedef struct {		/*  Ionospheric info (float)  */
	float
	    alpha_0, alpha_1, alpha_2, alpha_3,
	    beta_0, beta_1, beta_2, beta_3;
} ION_INFO;

typedef struct {		/*  Subframe 1 info (float)  */
	short
	    weeknum;
	unsigned char
	    codeL2, L2Pdata, SVacc_raw, SV_health;
	short
	    IODC;
	float
	    T_GD, t_oc, a_f2, a_f1, a_f0, SVacc;
} EPHEM_CLOCK;

typedef	struct {		/*  Ephemeris info (float)  */
	unsigned char
	    IODE, fit_interval;
	float
	    C_rs, delta_n;
	double
	    M_0;
	float
	    C_uc;
	double
	    e;
	float
	    C_us;
	double
	    sqrt_A;
	float
	    t_oe, C_ic;
	double
	    OMEGA_0;
	float
	    C_is;
	double
	    i_0;
	float
	    C_rc;
	double
	    omega;
	float
	    OMEGADOT, IDOT;
	double
	    Axis, n, r1me2, OMEGA_n, ODOT_n;
} EPHEM_ORBIT;

typedef struct {		/* Navigation data structure */
	short
	    sv_number;		/* SV number (0 = no entry) */
	float
	    t_ephem;		/* time of ephemeris collection */
	EPHEM_CLOCK
	    ephclk;		/* subframe 1 data */
	EPHEM_ORBIT
	    ephorb;		/* ephemeris data */
} NAV_INFO;

typedef struct {
	unsigned char
	    bSubcode,
	    operating_mode,
	    dgps_mode,
	    dyn_code,
	    trackmode;
	float
	    elev_mask,
	    cno_mask,
	    dop_mask,
	    dop_switch;
	unsigned char
	    dgps_age_limit;
} TSIP_RCVR_CFG;


#ifdef TRIMBLE_OUTPUT_FUNC
static char
        *dayname[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"},
	old_baudnum[] = {0, 1, 4, 5, 6, 8, 9, 11, 28, 12},
        *st_baud_text_app [] = {"", "", "  300", "  600", " 1200", " 2400", 
				" 4800", " 9600", "19200", "38400"},
	*old_parity_text[] = {"EVEN", "ODD", "", "", "NONE"},
	*parity_text [] = {"NONE", "ODD", "EVEN"},
	*old_input_ch[] = { "TSIP", "RTCM (6 of 8 bits)"},
	*old_output_ch[] = { "TSIP", "No output", "", "", "", "NMEA 0183"},
	*protocols_in_text[] = { "", "TSIP", "", ""},
	*protocols_out_text[] =	{ "", "TSIP", "NMEA"},
	*rcvr_port_text [] = { "Port A      ", "Port B      ", "Current Port"},
	*dyn_text [] = {"Unchanged", "Land", "Sea", "Air", "Static"},
	*NavModeText0xBB[] = {"automatic", "time only (0-D)", "", "2-D",
			      "3-D", "", "", "OverDetermined Time"},
	*PPSTimeBaseText[] = {"GPS", "UTC", "USER"},
	*PPSPolarityText[] = {"Positive", "Negative"},
  	*MaskText[] = { "Almanac  ", "Ephemeris", "UTC      ", "Iono     ",
			"GPS Msg  ", "Alm Hlth ", "Time Fix ", "SV Select",
			"Ext Event", "Pos Fix  ", "Raw Meas "};

#endif /* TRIMBLE_OUTPUT_FUNC */

/*
 * Unit control structure
 */
struct ripencc_unit {                   
        int unit;                       /* unit number */
        int     pollcnt;                /* poll message counter */
        int     polled;                 /* Hand in a sample? */
        char leapdelta;                 /* delta of next leap event */
        unsigned char utcflags;         /* delta of next leap event */
        l_fp    tstamp;                 /* timestamp of last poll */
        
        struct timespec ts;             /* last timestamp */
        pps_params_t pps_params;        /* pps parameters */
        pps_info_t pps_info;            /* last pps data */
        pps_handle_t handle;            /* pps handlebars */

};


/*******************        PROTOYPES            *****************/

/*  prototypes for report parsing primitives */
short rpt_0x3D (TSIPPKT *rpt, unsigned char *tx_baud_index,
		unsigned char *rx_baud_index, unsigned char *char_format_index,
		unsigned char *stop_bits, unsigned char *tx_mode_index,
		unsigned char *rx_mode_index);
short rpt_0x40 (TSIPPKT *rpt, unsigned char *sv_prn, short *week_num,
		float *t_zc, float *eccentricity, float *t_oa, float *i_0,
		float *OMEGA_dot, float *sqrt_A, float *OMEGA_0, float *omega,
		float *M_0);
short rpt_0x41 (TSIPPKT *rpt, float *time_of_week, float *UTC_offset,
		short *week_num);
short rpt_0x42 (TSIPPKT *rpt, float ECEF_pos[3], float *time_of_fix);
short rpt_0x43 (TSIPPKT *rpt, float ECEF_vel[3], float *freq_offset,
		float *time_of_fix);
short rpt_0x45 (TSIPPKT *rpt, unsigned char *major_nav_version,
		unsigned char *minor_nav_version, unsigned char *nav_day,
		unsigned char *nav_month, unsigned char *nav_year,
		unsigned char *major_dsp_version, unsigned char *minor_dsp_version,
		unsigned char *dsp_day, unsigned char *dsp_month,
		unsigned char *dsp_year);
short rpt_0x46 (TSIPPKT *rpt, unsigned char *status1, unsigned char *status2);
short rpt_0x47 (TSIPPKT *rpt, unsigned char *nsvs, unsigned char *sv_prn,
		float *snr);
short rpt_0x48 (TSIPPKT *rpt, unsigned char *message);
short rpt_0x49 (TSIPPKT *rpt, unsigned char *sv_health);
short rpt_0x4A (TSIPPKT *rpt, float *lat, float *lon, float *alt,
		float *clock_bias, float *time_of_fix);
short rpt_0x4A_2 (TSIPPKT *rpt, float *alt, float *dummy,
		  unsigned char *alt_flag);
short rpt_0x4B (TSIPPKT *rpt, unsigned char *machine_id,
		unsigned char *status3, unsigned char *status4);
short rpt_0x4C (TSIPPKT *rpt, unsigned char *dyn_code, float *el_mask,
		float *snr_mask, float *dop_mask, float *dop_switch);
short rpt_0x4D (TSIPPKT *rpt, float *osc_offset);
short rpt_0x4E (TSIPPKT *rpt, unsigned char *response);
short rpt_0x4F (TSIPPKT *rpt, double *a0, float *a1, float *time_of_data,
		short *dt_ls, short *wn_t, short *wn_lsf, short *dn, short *dt_lsf);
short rpt_0x54 (TSIPPKT *rpt, float *clock_bias, float *freq_offset,
		float *time_of_fix);
short rpt_0x55 (TSIPPKT *rpt, unsigned char *pos_code, unsigned char *vel_code,
		unsigned char *time_code, unsigned char *aux_code);
short rpt_0x56 (TSIPPKT *rpt, float vel_ENU[3], float *freq_offset,
		float *time_of_fix);
short rpt_0x57 (TSIPPKT *rpt, unsigned char *source_code,
		unsigned char *diag_code, short *week_num, float *time_of_fix);
short rpt_0x58 (TSIPPKT *rpt, unsigned char *op_code, unsigned char *data_type,
		unsigned char *sv_prn, unsigned char *data_length,
		unsigned char *data_packet);
short rpt_0x59 (TSIPPKT *rpt, unsigned char *code_type,
		unsigned char status_code[32]);
short rpt_0x5A (TSIPPKT *rpt, unsigned char *sv_prn, float *sample_length,
		float *signal_level, float *code_phase, float *Doppler,
		double *time_of_fix);
short rpt_0x5B (TSIPPKT *rpt, unsigned char *sv_prn, unsigned char *sv_health,
		unsigned char *sv_iode, unsigned char *fit_interval_flag,
		float *time_of_collection, float *time_of_eph, float *sv_accy);
short rpt_0x5C (TSIPPKT *rpt, unsigned char *sv_prn, unsigned char *slot,
		unsigned char *chan, unsigned char *acq_flag, unsigned char *eph_flag,
		float *signal_level, float *time_of_last_msmt, float *elev,
		float *azim, unsigned char *old_msmt_flag,
		unsigned char *integer_msec_flag, unsigned char *bad_data_flag,
		unsigned char *data_collect_flag);
short rpt_0x6D (TSIPPKT *rpt, unsigned char *manual_mode, unsigned char *nsvs,
		unsigned char *ndim, unsigned char sv_prn[], float *pdop,
		float *hdop, float *vdop, float *tdop);
short rpt_0x82 (TSIPPKT *rpt, unsigned char *diff_mode);
short rpt_0x83 (TSIPPKT *rpt, double ECEF_pos[3], double *clock_bias,
		float *time_of_fix);
short rpt_0x84 (TSIPPKT *rpt, double *lat, double *lon, double *alt,
		double *clock_bias, float *time_of_fix);
short rpt_Paly0xBB(TSIPPKT *rpt, TSIP_RCVR_CFG *TsipxBB);
short rpt_0xBC   (TSIPPKT *rpt, unsigned char *port_num,
		  unsigned char *in_baud, unsigned char *out_baud,
		  unsigned char *data_bits, unsigned char *parity,
		  unsigned char *stop_bits, unsigned char *flow_control,
		  unsigned char *protocols_in, unsigned char *protocols_out,
		  unsigned char *reserved);

/* prototypes for superpacket parsers */

short rpt_0x8F0B (TSIPPKT *rpt, unsigned short *event, double *tow,
		  unsigned char *date, unsigned char *month, short *year,
		  unsigned char *dim_mode, short *utc_offset, double *bias, double *drift,
		  float *bias_unc, float *dr_unc, double *lat, double *lon, double *alt,
		  char sv_id[8]);
short rpt_0x8F14 (TSIPPKT *rpt, short *datum_idx, double datum_coeffs[5]);
short rpt_0x8F15 (TSIPPKT *rpt, short *datum_idx, double datum_coeffs[5]);
short rpt_0x8F20 (TSIPPKT *rpt, unsigned char *info, double *lat,
		  double *lon, double *alt, double vel_enu[], double *time_of_fix,
		  short *week_num, unsigned char *nsvs, unsigned char sv_prn[], 
		  short sv_IODC[], short *datum_index);
short rpt_0x8F41 (TSIPPKT *rpt, unsigned char *bSearchRange,
		  unsigned char *bBoardOptions, unsigned long *iiSerialNumber,
		  unsigned char *bBuildYear, unsigned char *bBuildMonth,
		  unsigned char *bBuildDay, unsigned char *bBuildHour,
		  float *fOscOffset, unsigned short *iTestCodeId);
short rpt_0x8F42 (TSIPPKT *rpt, unsigned char *bProdOptionsPre,
		  unsigned char *bProdNumberExt, unsigned short *iCaseSerialNumberPre,
		  unsigned long *iiCaseSerialNumber, unsigned long *iiProdNumber,
		  unsigned short *iPremiumOptions, unsigned short *iMachineID,
		  unsigned short *iKey);
short rpt_0x8F45 (TSIPPKT *rpt, unsigned char *bSegMask);
short rpt_0x8F4A_16 (TSIPPKT *rpt, unsigned char *pps_enabled,
		     unsigned char *pps_timebase, unsigned char *pos_polarity,
		     double *pps_offset, float *bias_unc_threshold);
short rpt_0x8F4B (TSIPPKT *rpt, unsigned long *decorr_max);
short rpt_0x8F4D (TSIPPKT *rpt, unsigned long *event_mask);
short rpt_0x8FA5 (TSIPPKT *rpt, unsigned char *spktmask);
short rpt_0x8FAD (TSIPPKT *rpt, unsigned short *COUNT, double *FracSec,
		  unsigned char *Hour, unsigned char *Minute, unsigned char *Second,
		  unsigned char *Day, unsigned char *Month, unsigned short *Year,
		  unsigned char *Status, unsigned char *Flags);

/**/
/* prototypes for command-encode primitives with suffix convention:  */
/* c = clear, s = set, q = query, e = enable, d = disable            */
void cmd_0x1F  (TSIPPKT *cmd);
void cmd_0x26  (TSIPPKT *cmd);
void cmd_0x2F  (TSIPPKT *cmd);
void cmd_0x35s (TSIPPKT *cmd, unsigned char pos_code, unsigned char vel_code,
		unsigned char time_code, unsigned char opts_code);
void cmd_0x3C  (TSIPPKT *cmd, unsigned char sv_prn);
void cmd_0x3Ds (TSIPPKT *cmd, unsigned char baud_out, unsigned char baud_inp,
		unsigned char char_code, unsigned char stopbitcode,
		unsigned char output_mode, unsigned char input_mode);
void cmd_0xBBq (TSIPPKT *cmd, unsigned char subcode) ;

/* prototypes 8E commands */
void cmd_0x8E0Bq (TSIPPKT *cmd);
void cmd_0x8E41q (TSIPPKT *cmd);
void cmd_0x8E42q (TSIPPKT *cmd);
void cmd_0x8E4Aq (TSIPPKT *cmd);
void cmd_0x8E4As (TSIPPKT *cmd, unsigned char PPSOnOff, unsigned char TimeBase,
		  unsigned char Polarity, double PPSOffset, float Uncertainty);
void cmd_0x8E4Bq (TSIPPKT *cmd);
void cmd_0x8E4Ds (TSIPPKT *cmd, unsigned long AutoOutputMask);
void cmd_0x8EADq (TSIPPKT *cmd);

/* header/source border XXXXXXXXXXXXXXXXXXXXXXXXXX */

/* Trimble parse functions */
static 	int	parse0x8FAD	(TSIPPKT *, struct peer *);
static 	int	parse0x8F0B	(TSIPPKT *, struct peer *);
#ifdef TRIMBLE_OUTPUT_FUNC
static 	int	parseany	(TSIPPKT *, struct peer *);
static 	void	TranslateTSIPReportToText	(TSIPPKT *, char *);
#endif /* TRIMBLE_OUTPUT_FUNC */
static 	int	parse0x5C	(TSIPPKT *, struct peer *);
static 	int	parse0x4F	(TSIPPKT *, struct peer *);
static	void	tsip_input_proc	(TSIPPKT *, int);

/* Trimble helper functions */
static	void	bPutFloat 	(float *, unsigned char *);
static	void	bPutDouble 	(double *, unsigned char *);
static	void	bPutULong 	(unsigned long *, unsigned char *);
static	int	print_msg_table_header	(int rptcode, char *HdrStr, int force);
static	char *	show_time	(float time_of_week);

/* RIPE NCC functions */
static	void	ripencc_control	(int, const struct refclockstat *,
				 struct refclockstat *, struct peer *);
static	int	ripencc_ppsapi	(struct peer *, int, int);
static	int	ripencc_get_pps_ts	(struct ripencc_unit *, l_fp *);
static	int	ripencc_start	(int, struct peer *);
static 	void	ripencc_shutdown	(int, struct peer *);
static 	void	ripencc_poll	(int, struct peer *);
static 	void	ripencc_send	(struct peer *, TSIPPKT spt);
static 	void	ripencc_receive	(struct recvbuf *);

/* fill in reflock structure for our clock */
struct refclock refclock_ripencc = {
	ripencc_start,		/* start up driver */
	ripencc_shutdown,	/* shut down driver */
	ripencc_poll,		/* transmit poll message */
	ripencc_control,	/* control function */
	noentry,		/* initialize driver */
	noentry,		/* debug info */
	NOFLAGS			/* clock flags */
};

/*
 *  Tables to compute the ddd of year form icky dd/mm timecode. Viva la
 *  leap.
 */
static int day1tab[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
static int day2tab[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};


/*
 * ripencc_start - open the GPS devices and initialize data for processing
 */
static int
ripencc_start(int unit, struct peer *peer)
{
	register struct ripencc_unit *up;
	struct refclockproc *pp;
	char device[40];
	int fd;
	struct termios tio;
	TSIPPKT spt;

	pp = peer->procptr;

	/*
	 * Open serial port
	 */
	(void)snprintf(device, sizeof(device), DEVICE, unit);
	fd = refclock_open(device, SPEED232, LDISC_RAW);
	if (fd <= 0) {
		pp->io.fd = -1;
		return (0);
	}

	pp->io.fd = fd;

	/* from refclock_palisade.c */
	if (tcgetattr(fd, &tio) < 0) {
		msyslog(LOG_ERR, "Palisade(%d) tcgetattr(fd, &tio): %m",unit);
		return (0);
	}

	/*
	 * set flags
	 */
	tio.c_cflag |= (PARENB|PARODD);
	tio.c_iflag &= ~ICRNL;
	if (tcsetattr(fd, TCSANOW, &tio) == -1) {
		msyslog(LOG_ERR, "Palisade(%d) tcsetattr(fd, &tio): %m",unit);
		return (0);
	}

	/*
	 * Allocate and initialize unit structure
	 */
	up = emalloc_zero(sizeof(*up));

	pp->io.clock_recv = ripencc_receive;
	pp->io.srcclock = peer;
	pp->io.datalen = 0;
	if (!io_addclock(&pp->io)) {
		pp->io.fd = -1;
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
	memcpy((char *)&pp->refid, REFID, REFID_LEN);
	up->pollcnt = 2;
	up->unit = unit;
	up->leapdelta = 0;
	up->utcflags = 0;

	/*
	 * Initialize the Clock
	 */

	/* query software versions */
	cmd_0x1F(&spt);			
	ripencc_send(peer, spt);          

	/* query receiver health */
	cmd_0x26(&spt);			
	ripencc_send(peer, spt);

	/* query serial numbers */	
	cmd_0x8E42q(&spt);		
	ripencc_send(peer, spt);  
	
	/* query manuf params */
	cmd_0x8E41q(&spt);		
	ripencc_send(peer, spt); 

	/* i/o opts */ /* trimble manual page A30 */
	cmd_0x35s(&spt, 
		  0x1C, 	/* position */
		  0x00, 	/* velocity */
		  0x05, 	/* timing */
		  0x0a); 	/* auxilary */
	ripencc_send(peer, spt);
	
	/* turn off port A */
	cmd_0x3Ds (&spt,
		   0x0B,	/* baud_out */
		   0x0B,	/* baud_inp */
		   0x07,	/* char_code */
		   0x07,	/* stopbitcode */
		   0x01,	/* output_mode */
		   0x00);	/* input_mode */
	ripencc_send(peer, spt);

	/* set i/o options */
	cmd_0x8E4As (&spt,
		     0x01,	/* PPS on */
		     0x01,	/* Timebase UTC */
		     0x00,	/* polarity positive */
		     0.,	/* 100 ft. cable XXX make flag */
		     1e-6 * GPS_C); 	/* turn of biasuncert. > (1us) */
	ripencc_send(peer,spt);

	/* all outomatic packet output off */
	cmd_0x8E4Ds(&spt,
		    0x00000000); /* AutoOutputMask */
	ripencc_send(peer, spt);

	cmd_0xBBq (&spt,
		   0x00);	/* query primary configuration */
	ripencc_send(peer,spt);


	/* query PPS parameters */
	cmd_0x8E4Aq (&spt);	/* query PPS params */
	ripencc_send(peer,spt);

	/* query survey limit */
	cmd_0x8E4Bq (&spt);	/* query survey limit */
	ripencc_send(peer,spt);

#ifdef DEBUG_NCC
	if (debug)
		printf("ripencc_start: success\n");
#endif /* DEBUG_NCC */

	/*
	 * Start the PPSAPI interface if it is there. Default to use
	 * the assert edge and do not enable the kernel hardpps.
	 */
	if (time_pps_create(fd, &up->handle) < 0) {
		up->handle = 0;
		msyslog(LOG_ERR, "refclock_ripencc: time_pps_create failed: %m");
		return (1);
	}

	return(ripencc_ppsapi(peer, 0, 0));
}

/*
 * ripencc_control - fudge control
 */
static void
ripencc_control(
	int unit,		/* unit (not used) */
	const struct refclockstat *in, /* input parameters (not used) */
	struct refclockstat *out, /* output parameters (not used) */
	struct peer *peer	/* peer structure pointer */
	)
{
	struct refclockproc *pp;

#ifdef DEBUG_NCC
	msyslog(LOG_INFO,"%s()",__FUNCTION__);
#endif /* DEBUG_NCC */

	pp = peer->procptr;
	ripencc_ppsapi(peer, pp->sloppyclockflag & CLK_FLAG2,
		       pp->sloppyclockflag & CLK_FLAG3);
}


/*
 * Initialize PPSAPI
 */
int
ripencc_ppsapi(
	struct peer *peer,	/* peer structure pointer */
	int enb_clear,		/* clear enable */
	int enb_hardpps		/* hardpps enable */
	)
{
	struct refclockproc *pp;
	struct ripencc_unit *up;
	int capability;

	pp = peer->procptr;
	up = pp->unitptr;
	if (time_pps_getcap(up->handle, &capability) < 0) {
		msyslog(LOG_ERR,
			"refclock_ripencc: time_pps_getcap failed: %m");
		return (0);
	}
	memset(&up->pps_params, 0, sizeof(pps_params_t));
	if (enb_clear)
		up->pps_params.mode = capability & PPS_CAPTURECLEAR;
	else
		up->pps_params.mode = capability & PPS_CAPTUREASSERT;
	if (!up->pps_params.mode) {
		msyslog(LOG_ERR,
			"refclock_ripencc: invalid capture edge %d",
			!enb_clear);
		return (0);
	}
	up->pps_params.mode |= PPS_TSFMT_TSPEC;
	if (time_pps_setparams(up->handle, &up->pps_params) < 0) {
		msyslog(LOG_ERR,
			"refclock_ripencc: time_pps_setparams failed: %m");
		return (0);
	}
	if (enb_hardpps) {
		if (time_pps_kcbind(up->handle, PPS_KC_HARDPPS,
				    up->pps_params.mode & ~PPS_TSFMT_TSPEC,
				    PPS_TSFMT_TSPEC) < 0) {
			msyslog(LOG_ERR,
				"refclock_ripencc: time_pps_kcbind failed: %m");
			return (0);
		}
		hardpps_enable = 1;
	}
	peer->precision = PPS_PRECISION;

#if DEBUG_NCC
	if (debug) {
		time_pps_getparams(up->handle, &up->pps_params);
		printf(
			"refclock_ripencc: capability 0x%x version %d mode 0x%x kern %d\n",
			capability, up->pps_params.api_version,
			up->pps_params.mode, enb_hardpps);
	}
#endif /* DEBUG_NCC */

	return (1);
}

/*
 * This function is called every 64 seconds from ripencc_receive
 * It will fetch the pps time 
 *
 * Return 0 on failure and 1 on success.
 */
static int
ripencc_get_pps_ts(
	struct ripencc_unit *up,
	l_fp *tsptr
	)
{
	pps_info_t pps_info;
	struct timespec timeout, ts;
	double dtemp;
	l_fp tstmp;

#ifdef DEBUG_PPS
	msyslog(LOG_INFO,"ripencc_get_pps_ts");
#endif /* DEBUG_PPS */


	/*
	 * Convert the timespec nanoseconds field to ntp l_fp units.
	 */ 
	if (up->handle == 0)
		return (0);
	timeout.tv_sec = 0;
	timeout.tv_nsec = 0;
	memcpy(&pps_info, &up->pps_info, sizeof(pps_info_t));
	if (time_pps_fetch(up->handle, PPS_TSFMT_TSPEC, &up->pps_info,
			   &timeout) < 0)
		return (0);
	if (up->pps_params.mode & PPS_CAPTUREASSERT) {
		if (pps_info.assert_sequence ==
		    up->pps_info.assert_sequence)
			return (0);
		ts = up->pps_info.assert_timestamp;
	} else if (up->pps_params.mode & PPS_CAPTURECLEAR) {
		if (pps_info.clear_sequence ==
		    up->pps_info.clear_sequence)
			return (0);
		ts = up->pps_info.clear_timestamp;
	} else {
		return (0);
	}
	if ((up->ts.tv_sec == ts.tv_sec) && (up->ts.tv_nsec == ts.tv_nsec))
		return (0);
	up->ts = ts;

	tstmp.l_ui = ts.tv_sec + JAN_1970;
	dtemp = ts.tv_nsec * FRAC / 1e9;
	tstmp.l_uf = (u_int32)dtemp;

#ifdef DEBUG_PPS
	msyslog(LOG_INFO,"ts.tv_sec: %d",(int)ts.tv_sec);
	msyslog(LOG_INFO,"ts.tv_nsec: %ld",ts.tv_nsec);
#endif /* DEBUG_PPS */

	*tsptr = tstmp;
	return (1);
}

/*
 * ripencc_shutdown - shut down a GPS clock
 */
static void
ripencc_shutdown(int unit, struct peer *peer)
{
	register struct ripencc_unit *up;
	struct refclockproc *pp;

	pp = peer->procptr;
	up = pp->unitptr;

	if (up != NULL) {
		if (up->handle != 0)
			time_pps_destroy(up->handle);
		free(up);
	}
	if (-1 != pp->io.fd)
		io_closeclock(&pp->io);

	return;
}

/*
 * ripencc_poll - called by the transmit procedure
 */
static void
ripencc_poll(int unit, struct peer *peer)
{
	register struct ripencc_unit *up;
	struct refclockproc *pp;
	TSIPPKT spt;

#ifdef DEBUG_NCC
	if (debug)
		fprintf(stderr, "ripencc_poll(%d)\n", unit);
#endif /* DEBUG_NCC */
	pp = peer->procptr;
	up = pp->unitptr;
	if (up->pollcnt == 0)
		refclock_report(peer, CEVNT_TIMEOUT);
	else
		up->pollcnt--;

	pp->polls++;
	up->polled = 1;

	/* poll for UTC superpacket */
	cmd_0x8EADq (&spt);
	ripencc_send(peer,spt);
}

/*
 * ripencc_send - send message to clock
 * use the structures being created by the trimble functions!
 * makes the code more readable/clean
 */
static void
ripencc_send(struct peer *peer, TSIPPKT spt)
{
	unsigned char *ip, *op;
	unsigned char obuf[512];

#ifdef DEBUG_RAW
	{
		register struct ripencc_unit *up;
		register struct refclockproc *pp;	

		pp = peer->procptr;
		up = pp->unitptr;
		if (debug)
			printf("ripencc_send(%d, %02X)\n", up->unit, cmd);
	}
#endif /* DEBUG_RAW */

	ip = spt.buf;
	op = obuf;

	*op++ = 0x10;
	*op++ = spt.code;

	while (spt.len--) {
		if (op-obuf > sizeof(obuf)-5) {
			msyslog(LOG_ERR, "ripencc_send obuf overflow!");
			refclock_report(peer, CEVNT_FAULT);
			return;
		}
			
		if (*ip == 0x10) /* byte stuffing */
			*op++ = 0x10;
		*op++ = *ip++;
	}
	
	*op++ = 0x10;
	*op++ = 0x03;

#ifdef DEBUG_RAW
	if (debug) { /* print raw packet */
		unsigned char *cp;
		int i;

		printf("ripencc_send: len %d\n", op-obuf);
		for (i=1, cp=obuf; cp<op; i++, cp++) {
			printf(" %02X", *cp);
			if (i%10 == 0) 
				printf("\n");
		}
		printf("\n");
	}
#endif /* DEBUG_RAW */

	if (write(peer->procptr->io.fd, obuf, op-obuf) == -1) {
		refclock_report(peer, CEVNT_FAULT);
	}
}

/*
 * ripencc_receive()
 *
 * called when a packet is received on the serial port
 * takes care of further processing
 *
 */
static void
ripencc_receive(struct recvbuf *rbufp)
{
	register struct ripencc_unit *up;
	register struct refclockproc *pp;	
	struct peer *peer;
	static TSIPPKT rpt;	/* for current incoming TSIP report */ 
	TSIPPKT spt;		/* send packet */
	int ns_since_pps;			
	int i;
	char *cp;
	/* these variables hold data until we decide it's worth keeping */
	char    rd_lastcode[BMAX];
	l_fp    rd_tmp;
	u_short rd_lencode;

	/* msyslog(LOG_INFO, "%s",__FUNCTION__); */

	/*
	 * Initialize pointers and read the timecode and timestamp
	 */
	peer = rbufp->recv_peer;
	pp = peer->procptr;
	up = pp->unitptr;
	rd_lencode = refclock_gtlin(rbufp, rd_lastcode, BMAX, &rd_tmp);

#ifdef DEBUG_RAW
	if (debug)
		fprintf(stderr, "ripencc_receive(%d)\n", up->unit);
#endif /* DEBUG_RAW */

#ifdef DEBUG_RAW
	if (debug) {		/* print raw packet */
		int i;
		unsigned char *cp;

		printf("ripencc_receive: len %d\n", rbufp->recv_length);
		for (i=1, cp=(char*)&rbufp->recv_space;
		     i <= rbufp->recv_length;
		     i++, cp++) {
			printf(" %02X", *cp);
			if (i%10 == 0) 
				printf("\n");
		}
		printf("\n");
	}
#endif /* DEBUG_RAW */

	cp = (char*) &rbufp->recv_space;
	i=rbufp->recv_length;

	while (i--) {		/* loop over received chars */

		tsip_input_proc(&rpt, (unsigned char) *cp++);

		if (rpt.status != TSIP_PARSED_FULL)
			continue;

		switch (rpt.code) {

		    case 0x8F:	/* superpacket */

			switch (rpt.buf[0]) {

			    case 0xAD:	/* UTC Time */
				/*
				** When polling on port B the timecode is
				** the time of the previous PPS.  If we
				** completed receiving the packet less than
				** 150ms after the turn of the second, it
				** may have the code of the previous second.
				** We do not trust that and simply poll
				** again without even parsing it.
				**
				** More elegant would be to re-schedule the
				** poll, but I do not know (yet) how to do
				** that cleanly.
				**
				*/
				/* BLA ns_since_pps = ncc_tstmp(rbufp, &trtmp); */
/*   if (up->polled && ns_since_pps > -1 && ns_since_pps < 150) { */

				ns_since_pps = 200;
				if (up->polled && ns_since_pps < 150) {
					msyslog(LOG_INFO, "%s(): up->polled",
						__FUNCTION__);
					ripencc_poll(up->unit, peer);
					break;
				}

			        /*
 				 * Parse primary utc time packet
				 * and fill refclock structure 
				 * from results. 
				 */
				if (parse0x8FAD(&rpt, peer) < 0) {
					msyslog(LOG_INFO, "%s(): parse0x8FAD < 0",__FUNCTION__);
					refclock_report(peer, CEVNT_BADREPLY);
					break;
				}
				/*
				 * If the PPSAPI is working, rather use its 
				 * timestamps.
				 * assume that the PPS occurs on the second 
				 * so blow any msec
				 */
				if (ripencc_get_pps_ts(up, &rd_tmp) == 1) {
					pp->lastrec = up->tstamp = rd_tmp;
					pp->nsec = 0;
				}
				else
					msyslog(LOG_INFO, "%s(): ripencc_get_pps_ts returns failure",__FUNCTION__);


				if (!up->polled) { 
					msyslog(LOG_INFO, "%s(): unrequested packet",__FUNCTION__);
					/* unrequested packet */
					break;
				}

				/* we have been polled ! */
				up->polled = 0;
				up->pollcnt = 2;

				/* poll for next packet */
				cmd_0x8E0Bq(&spt);
				ripencc_send(peer,spt);
				
				if (ns_since_pps < 0) { /* no PPS */
					msyslog(LOG_INFO, "%s(): ns_since_pps < 0",__FUNCTION__);
					refclock_report(peer, CEVNT_BADTIME);
					break;
				}

				/*
				** Process the new sample in the median
				** filter and determine the reference clock
				** offset and dispersion.
				*/
				if (!refclock_process(pp)) {
					msyslog(LOG_INFO, "%s(): !refclock_process",__FUNCTION__);
					refclock_report(peer, CEVNT_BADTIME);
					break;
				}

				refclock_receive(peer);
				break;
			
			    case 0x0B: /* comprehensive time packet */
				parse0x8F0B(&rpt, peer);
				break;

			    default: /* other superpackets */
#ifdef DEBUG_NCC
				msyslog(LOG_INFO, "%s(): calling parseany",
					__FUNCTION__);
#endif /* DEBUG_NCC */
#ifdef TRIMBLE_OUTPUT_FUNC
				parseany(&rpt, peer);
#endif /* TRIMBLE_OUTPUT_FUNC */
				break;
			}
			break;

		    case 0x4F:	/* UTC parameters, for leap info */
			parse0x4F(&rpt, peer);
			break;

		    case 0x5C:	/* sat tracking data */
			parse0x5C(&rpt, peer);
			break;

		    default:	/* other packets */
#ifdef TRIMBLE_OUTPUT_FUNC
			parseany(&rpt, peer);
#endif /* TRIMBLE_OUTPUT_FUNC */
			break;
		}
   		rpt.status = TSIP_PARSED_EMPTY;
	}
}

/* 
 * All trimble functions that are directly referenced from driver code
 * (so not from parseany)
 */

/* request software versions */
void
cmd_0x1F(
	 TSIPPKT *cmd
	 )
{
	cmd->len = 0;
	cmd->code = 0x1F;
}

/* request receiver health */
void
cmd_0x26(
	 TSIPPKT *cmd
	 )
{
	cmd->len = 0;
	cmd->code = 0x26;
}

/* request UTC params */
void
cmd_0x2F(
	 TSIPPKT *cmd
	 )
{
	cmd->len = 0;
	cmd->code = 0x2F;
}

/* set serial I/O options */
void
cmd_0x35s(
	 TSIPPKT *cmd,
	 unsigned char pos_code,
	 unsigned char vel_code,
	 unsigned char time_code,
	 unsigned char opts_code
	 )
{
	cmd->buf[0] = pos_code;
	cmd->buf[1] = vel_code;
	cmd->buf[2] = time_code;
	cmd->buf[3] = opts_code;
	cmd->len = 4;
	cmd->code = 0x35;
}

/* request tracking status */
void
cmd_0x3C(
	 TSIPPKT *cmd,
	 unsigned char sv_prn
	 )
{
	cmd->buf[0] = sv_prn;
	cmd->len = 1;
	cmd->code = 0x3C;
}

/* set Channel A configuration for dual-port operation */
void
cmd_0x3Ds(
	  TSIPPKT *cmd,
	  unsigned char baud_out,
	  unsigned char baud_inp,
	  unsigned char char_code,
	  unsigned char stopbitcode,
	  unsigned char output_mode,
	  unsigned char input_mode
	  )
{
	cmd->buf[0] = baud_out;		/* XMT baud rate */
	cmd->buf[1] = baud_inp;		/* RCV baud rate */
	cmd->buf[2] = char_code;	/* parity and #bits per byte */
	cmd->buf[3] = stopbitcode;	/* number of stop bits code */
	cmd->buf[4] = output_mode;	/* Ch. A transmission mode */
	cmd->buf[5] = input_mode;	/* Ch. A reception mode */
	cmd->len = 6;
	cmd->code = 0x3D;
}


/* query primary configuration */
void
cmd_0xBBq(
	  TSIPPKT *cmd,
	  unsigned char subcode
	  )
{
	cmd->len = 1;
	cmd->code = 0xBB;
	cmd->buf[0] = subcode;
}


/**** Superpackets ****/
/* 8E-0B to query 8F-0B controls */
void
cmd_0x8E0Bq(
	    TSIPPKT *cmd
	    )
{
	cmd->len = 1;
	cmd->code = 0x8E;
	cmd->buf[0] = 0x0B;
}


/* 8F-41 to query board serial number */
void
cmd_0x8E41q(
	    TSIPPKT *cmd
	    )
{
	cmd->len = 1;
	cmd->code = 0x8E;
	cmd->buf[0] = 0x41;
}


/* 8F-42 to query product serial number */
void
cmd_0x8E42q(
	    TSIPPKT *cmd
	    )
{
	cmd->len = 1;
	cmd->code = 0x8E;
	cmd->buf[0] = 0x42;
}


/* 8F-4A to query PPS parameters */
void
cmd_0x8E4Aq(
	    TSIPPKT *cmd
	    )
{
	cmd->len = 1;
	cmd->code = 0x8E;
	cmd->buf[0] = 0x4A;
}


/* set i/o options */
void
cmd_0x8E4As(
	    TSIPPKT *cmd,
	    unsigned char PPSOnOff,
	    unsigned char TimeBase,
	    unsigned char Polarity,
	    double PPSOffset,
	    float Uncertainty
	    )
{
	cmd->len = 16;
	cmd->code = 0x8E;
	cmd->buf[0] = 0x4A;
	cmd->buf[1] = PPSOnOff;
	cmd->buf[2] = TimeBase;
	cmd->buf[3] = Polarity;
	bPutDouble (&PPSOffset, &cmd->buf[4]);
	bPutFloat (&Uncertainty, &cmd->buf[12]);
}

/* 8F-4B query survey limit */
void
cmd_0x8E4Bq(
	    TSIPPKT *cmd
	    )
{
	cmd->len = 1;
	cmd->code = 0x8E;
	cmd->buf[0] = 0x4B;
}

/* poll for UTC superpacket */
/* 8E-AD to query 8F-AD controls */
void
cmd_0x8EADq(
	    TSIPPKT *cmd
	    )
{
	cmd->len = 1;
	cmd->code = 0x8E;
	cmd->buf[0] = 0xAD;
}

/* all outomatic packet output off */
void
cmd_0x8E4Ds(
	    TSIPPKT *cmd,
	    unsigned long AutoOutputMask
	    )
{
	cmd->len = 5;
	cmd->code = 0x8E;
	cmd->buf[0] = 0x4D;
	bPutULong (&AutoOutputMask, &cmd->buf[1]);
}


/*
 * for DOS machines, reverse order of bytes as they come through the
 * serial port.
 */
#ifdef BYTESWAP
static short
bGetShort(
	  unsigned char *bp
	  )
{
	short outval;
	unsigned char *optr;

	optr = (unsigned char*)&outval + 1;
	*optr-- = *bp++;
	*optr = *bp;
	return outval;
}

#ifdef TRIMBLE_OUTPUT_FUNC
static unsigned short
bGetUShort(
	   unsigned char *bp
	   )
{
	unsigned short outval;
	unsigned char *optr;

	optr = (unsigned char*)&outval + 1;
	*optr-- = *bp++;
	*optr = *bp;
	return outval;
}

static long
bGetLong(
	 unsigned char *bp
	 )
{
	long outval;
	unsigned char *optr;

	optr = (unsigned char*)&outval + 3;
	*optr-- = *bp++;
	*optr-- = *bp++;
	*optr-- = *bp++;
	*optr = *bp;
	return outval;
}

static unsigned long
bGetULong(
	  unsigned char *bp
	  )
{
	unsigned long outval;
	unsigned char *optr;

	optr = (unsigned char*)&outval + 3;
	*optr-- = *bp++;
	*optr-- = *bp++;
	*optr-- = *bp++;
	*optr = *bp;
	return outval;
}
#endif /* TRIMBLE_OUTPUT_FUNC */

static float
bGetSingle(
	   unsigned char *bp
	   )
{
	float outval;
	unsigned char *optr;

	optr = (unsigned char*)&outval + 3;
	*optr-- = *bp++;
	*optr-- = *bp++;
	*optr-- = *bp++;
	*optr = *bp;
	return outval;
}

static double
bGetDouble(
	   unsigned char *bp
	   )
{
	double outval;
	unsigned char *optr;

	optr = (unsigned char*)&outval + 7;
	*optr-- = *bp++;
	*optr-- = *bp++;
	*optr-- = *bp++;
	*optr-- = *bp++;
	*optr-- = *bp++;
	*optr-- = *bp++;
	*optr-- = *bp++;
	*optr = *bp;
	return outval;
}

#else /* not BYTESWAP */

#define bGetShort(bp) 	(*(short*)(bp))
#define bGetLong(bp) 	(*(long*)(bp))
#define bGetULong(bp) 	(*(unsigned long*)(bp))
#define bGetSingle(bp) 	(*(float*)(bp))
#define bGetDouble(bp)	(*(double*)(bp))

#endif /* BYTESWAP */
/*
 * Byte-reversal is necessary for little-endian (Intel-based) machines.
 * TSIP streams are Big-endian (Motorola-based).
 */
#ifdef BYTESWAP

void
bPutFloat(
	  float *in,
	  unsigned char *out
	  )
{
	unsigned char *inptr;

	inptr = (unsigned char*)in + 3;
	*out++ = *inptr--;
	*out++ = *inptr--;
	*out++ = *inptr--;
	*out = *inptr;
}

static void
bPutULong(
	  unsigned long *in,
	  unsigned char *out
	  )
{
	unsigned char *inptr;

	inptr = (unsigned char*)in + 3;
	*out++ = *inptr--;
	*out++ = *inptr--;
	*out++ = *inptr--;
	*out = *inptr;
}

static void
bPutDouble(
	   double *in,
	   unsigned char *out
	   )
{
	unsigned char *inptr;

	inptr = (unsigned char*)in + 7;
	*out++ = *inptr--;
	*out++ = *inptr--;
	*out++ = *inptr--;
	*out++ = *inptr--;
	*out++ = *inptr--;
	*out++ = *inptr--;
	*out++ = *inptr--;
	*out = *inptr;
}

#else	/* not BYTESWAP */

void bPutShort (short a, unsigned char *cmdbuf) {*(short*) cmdbuf = a;}
void bPutULong (long a, unsigned char *cmdbuf) 	{*(long*) cmdbuf = a;}
void bPutFloat (float a, unsigned char *cmdbuf) {*(float*) cmdbuf = a;}
void bPutDouble (double a, unsigned char *cmdbuf){*(double*) cmdbuf = a;}

#endif /* BYTESWAP */

/*
 * Parse primary utc time packet
 * and fill refclock structure 
 * from results. 
 *
 * 0 = success
 * -1 = errors
 */

static int
parse0x8FAD(
	    TSIPPKT *rpt,
	    struct peer *peer
	    )
{
	register struct refclockproc *pp;	
	register struct ripencc_unit *up;

	unsigned day, month, year;	/* data derived from received timecode */
	unsigned hour, minute, second;
	unsigned char trackstat, utcflags;

   	static char logbuf[1024];	/* logging string buffer */
	int i;
	unsigned char *buf;
		
	buf = rpt->buf;
	pp = peer->procptr;

	if (rpt->len != 22) 
		return (-1);
	
	if (bGetShort(&buf[1]) != 0) {
#ifdef DEBUG_NCC
		if (debug) 
			printf("parse0x8FAD: event count != 0\n");
#endif /* DEBUG_NCC */
		return(-1);
	}

	if (bGetDouble(&buf[3]) != 0.0) {
#ifdef DEBUG_NCC
		if (debug) 
			printf("parse0x8FAD: fracsecs != 0\n");
#endif /* DEBUG_NCC */
		return(-1);
	}

	hour =		(unsigned int) buf[11];
	minute =	(unsigned int) buf[12];
	second =	(unsigned int) buf[13];
	day =		(unsigned int) buf[14];
	month =		(unsigned int) buf[15];
	year =		bGetShort(&buf[16]);
	trackstat =	buf[18];
	utcflags =	buf[19];


	sprintf(logbuf, "U1 %d.%d.%d %02d:%02d:%02d %d %02x",
		day, month, year, hour, minute, second, trackstat, utcflags);

#ifdef DEBUG_NCC
	if (debug) 
   		puts(logbuf);
#endif /* DEBUG_NCC */

	record_clock_stats(&peer->srcadr, logbuf);

	if (!utcflags & UTCF_UTC_AVAIL)
		return(-1);

	/* poll for UTC parameters once and then if UTC flag changed */
	up = (struct ripencc_unit *) pp->unitptr;
	if (utcflags != up->utcflags) {
		TSIPPKT spt;	/* local structure for send packet */
		cmd_0x2F (&spt); /* request UTC params */
		ripencc_send(peer,spt);
		up->utcflags = utcflags;
	}
	
	/*
	 * If we hit the leap second, we choose to skip this sample
	 * rather than rely on other code to be perfectly correct.
	 * No offense, just defense ;-).
	 */
	if (second == 60)
		return(-1);

	/* now check and convert the time we received */

	pp->year = year;
	if (month < 1 || month > 12 || day < 1 || day > 31) 
		return(-1);

	if (pp->year % 4) {	/* XXX: use is_leapyear() ? */
		if (day > day1tab[month - 1]) 
			return(-1);
		for (i = 0; i < month - 1; i++)
			day += day1tab[i];
	} else {
		if (day > day2tab[month - 1]) 
			return(-1);
		for (i = 0; i < month - 1; i++)
			day += day2tab[i];
	}
	pp->day = day;
	pp->hour = hour;
	pp->minute = minute;
	pp-> second = second;
	pp->nsec = 0;

	if ((utcflags&UTCF_LEAP_PNDG) && up->leapdelta != 0) 
		pp-> leap = (up->leapdelta > 0)
		    ? LEAP_ADDSECOND
		    : LEAP_DELSECOND; 
	else
		pp-> leap = LEAP_NOWARNING;  

	return (0);
}

/*
 * Parse comprehensive time packet 
 *
 *  0 = success
 * -1 = errors
 */

int
parse0x8F0B(
	    TSIPPKT *rpt,
	    struct peer *peer
	    )
{
	register struct refclockproc *pp;	

	unsigned day, month, year;	/* data derived from received timecode */
	unsigned hour, minute, second;
	unsigned utcoff;
	unsigned char mode;
	double  bias, rate;
	float biasunc, rateunc;
	double lat, lon, alt;
	short lat_deg, lon_deg;
	float lat_min, lon_min;
	unsigned char north_south, east_west;
	char sv[9];

   	static char logbuf[1024];	/* logging string buffer */
	unsigned char b;
	int i;
	unsigned char *buf;
	double tow;
		
	buf = rpt->buf;
	pp = peer->procptr;

	if (rpt->len != 74) 
		return (-1);
	
	if (bGetShort(&buf[1]) != 0)
		return(-1);;

	tow =  bGetDouble(&buf[3]);

	if (tow == -1.0) {
		return(-1);
	}
	else if ((tow >= 604800.0) || (tow < 0.0)) {
		return(-1);
	}
	else
	{
		if (tow < 604799.9) tow = tow + .00000001;
		second = (unsigned int) fmod(tow, 60.);
		minute =  (unsigned int) fmod(tow/60., 60.);
		hour = (unsigned int )fmod(tow / 3600., 24.);
	} 

	day =		(unsigned int) buf[11];
	month =		(unsigned int) buf[12];
	year =		bGetShort(&buf[13]);
	mode =		buf[15];
	utcoff =	bGetShort(&buf[16]);
	bias = 		bGetDouble(&buf[18]) / GPS_C * 1e9;	/* ns */
	rate = 		bGetDouble(&buf[26]) / GPS_C * 1e9;	/* ppb */ 
	biasunc = 	bGetSingle(&buf[34]) / GPS_C * 1e9;	/* ns */
	rateunc = 	bGetSingle(&buf[38]) / GPS_C * 1e9;	/* ppb */
	lat = 		bGetDouble(&buf[42]) * R2D;
	lon = 		bGetDouble(&buf[50]) * R2D;
	alt = 		bGetDouble(&buf[58]);

	if (lat < 0.0) {
		north_south = 'S';
		lat = -lat;
	}
	else {
		north_south = 'N';
	}
	lat_deg = (short)lat;
	lat_min = (lat - lat_deg) * 60.0;

	if (lon < 0.0) {
		east_west = 'W';
		lon = -lon;
	}
	else {
		east_west = 'E';
	}

	lon_deg = (short)lon;
	lon_min = (lon - lon_deg) * 60.0;

	for (i=0; i<8; i++) {
		sv[i] = buf[i + 66];
		if (sv[i]) {
			TSIPPKT spt; /* local structure for sendpacket */
			b = (unsigned char) (sv[i]<0 ? -sv[i] : sv[i]);
			/* request tracking status */
			cmd_0x3C  (&spt, b);
			ripencc_send(peer,spt);
		}
	}


	sprintf(logbuf, "C1 %02d%02d%04d %02d%02d%02d %d %7.0f %.1f %.0f %.1f %d %02d%09.6f %c %02d%09.6f %c %.0f  %d %d %d %d %d %d %d %d",
		day, month, year, hour, minute, second, mode, bias, biasunc,
		rate, rateunc, utcoff, lat_deg, lat_min, north_south, lon_deg,
		lon_min, east_west, alt, sv[0], sv[1], sv[2], sv[3], sv[4],
		sv[5], sv[6], sv[7]);

#ifdef DEBUG_NCC
	if (debug) 
   		puts(logbuf);
#endif /* DEBUG_NCC */

	record_clock_stats(&peer->srcadr, logbuf);

	return (0);
}

#ifdef TRIMBLE_OUTPUT_FUNC
/* 
 * Parse any packet using Trimble machinery
 */
int
parseany(
	 TSIPPKT *rpt,
	 struct peer *peer
	 )
{
   	static char logbuf[1024];	/* logging string buffer */

   	TranslateTSIPReportToText (rpt, logbuf);	/* anything else */
#ifdef DEBUG_NCC
	if (debug) 
   		puts(&logbuf[1]);
#endif /* DEBUG_NCC */
	record_clock_stats(&peer->srcadr, &logbuf[1]);
	return(0);
}
#endif /* TRIMBLE_OUTPUT_FUNC */


/*
 * Parse UTC Parameter Packet
 * 
 * See the IDE for documentation!
 *
 * 0 = success
 * -1 = errors
 */

int
parse0x4F(
	  TSIPPKT *rpt,
	  struct peer *peer
	  )
{
	register struct ripencc_unit *up;

	double a0;
	float a1, tot;
	int dt_ls, wn_t, wn_lsf, dn, dt_lsf;

   	static char logbuf[1024];	/* logging string buffer */
	unsigned char *buf;
		
	buf = rpt->buf;
	
	if (rpt->len != 26) 
		return (-1);
	a0 = bGetDouble (buf);
	a1 = bGetSingle (&buf[8]);
	dt_ls = bGetShort (&buf[12]);
	tot = bGetSingle (&buf[14]);
	wn_t = bGetShort (&buf[18]);
	wn_lsf = bGetShort (&buf[20]);
	dn = bGetShort (&buf[22]);
	dt_lsf = bGetShort (&buf[24]);

	sprintf(logbuf, "L1 %d %d %d %g %g %g %d %d %d",
		dt_lsf - dt_ls, dt_ls, dt_lsf, a0, a1, tot, wn_t, wn_lsf, dn); 

#ifdef DEBUG_NCC
	if (debug) 
   		puts(logbuf);
#endif /* DEBUG_NCC */

	record_clock_stats(&peer->srcadr, logbuf);

	up = (struct ripencc_unit *) peer->procptr->unitptr;
	up->leapdelta = dt_lsf - dt_ls;

	return (0);
}

/*
 * Parse Tracking Status packet
 *
 * 0 = success
 * -1 = errors
 */

int
parse0x5C(
	  TSIPPKT *rpt,
	  struct peer *peer
	  )
{
	unsigned char prn, channel, aqflag, ephstat;
	float snr, azinuth, elevation;

   	static char logbuf[1024];	/* logging string buffer */
	unsigned char *buf;
		
	buf = rpt->buf;
	
	if (rpt->len != 24) 
		return(-1);

	prn = buf[0];
	channel = (unsigned char)(buf[1] >> 3);
	if (channel == 0x10) 
		channel = 2;
	else 
		channel++;
	aqflag = buf[2];
	ephstat = buf[3];
	snr = bGetSingle(&buf[4]);
	elevation = bGetSingle(&buf[12]) * R2D;
	azinuth = bGetSingle(&buf[16]) * R2D;

	sprintf(logbuf, "S1 %02d %d %d %02x %4.1f %5.1f %4.1f",
		prn, channel, aqflag, ephstat, snr, azinuth, elevation);

#ifdef DEBUG_NCC
	if (debug) 
   		puts(logbuf);
#endif /* DEBUG_NCC */

	record_clock_stats(&peer->srcadr, logbuf);

	return (0);
}

/******* Code below is from Trimble Tsipchat *************/

/*
 * *************************************************************************
 *
 * Trimble Navigation, Ltd.
 * OEM Products Development Group
 * P.O. Box 3642
 * 645 North Mary Avenue
 * Sunnyvale, California 94088-3642
 *
 * Corporate Headquarter:
 *    Telephone:  (408) 481-8000
 *    Fax:        (408) 481-6005
 *
 * Technical Support Center:
 *    Telephone:  (800) 767-4822	(U.S. and Canada)
 *                (408) 481-6940    (outside U.S. and Canada)
 *    Fax:        (408) 481-6020
 *    BBS:        (408) 481-7800
 *    e-mail:     trimble_support@trimble.com
 *		ftp://ftp.trimble.com/pub/sct/embedded/bin
 *
 * *************************************************************************
 *
 * -------  BYTE-SWAPPING  -------
 * TSIP is big-endian (Motorola) protocol.  To use on little-endian (Intel)
 * systems, the bytes of all multi-byte types (shorts, floats, doubles, etc.)
 * must be reversed.  This is controlled by the MACRO BYTESWAP; if defined, it
 * assumes little-endian protocol.
 * --------------------------------
 *
 * T_PARSER.C and T_PARSER.H contains primitive functions that interpret
 * reports received from the receiver.  A second source file pair,
 * T_FORMAT.C and T_FORMAT.H, contin the matching TSIP command formatters.
 *
 * The module is in very portable, basic C language.  It can be used as is, or
 * with minimal changes if a TSIP communications application is needed separate
 * from TSIPCHAT. The construction of most argument lists avoid the use of
 * structures, but the developer is encouraged to reconstruct them using such
 * definitions to meet project requirements.  Declarations of T_PARSER.C
 * functions are included in T_PARSER.H to provide prototyping definitions.
 *
 * There are two types of functions: a serial input processing routine,
 *                            tsip_input_proc()
 * which assembles incoming bytes into a TSIPPKT structure, and the
 * report parsers, rpt_0x??().
 *
 * 1) The function tsip_input_proc() accumulates bytes from the receiver,
 * strips control bytes (DLE), and checks if the report end sequence (DLE ETX)
 * has been received.  rpt.status is defined as TSIP_PARSED_FULL (== 1)
 * if a complete packet is available.
 *
 * 2) The functions rpt_0x??() are report string interpreters patterned after
 * the document called "Trimble Standard Interface Protocol".  It should be
 * noted that if the report buffer is sent into the receiver with the wrong
 * length (byte count), the rpt_0x??() returns the Boolean equivalence for
 * TRUE.
 *
 * *************************************************************************
 *
 */


/*
 * reads bytes until serial buffer is empty or a complete report
 * has been received; end of report is signified by DLE ETX.
 */
static void
tsip_input_proc(
		TSIPPKT *rpt,
		int inbyte
		)
{
	unsigned char newbyte;

	if (inbyte < 0 || inbyte > 0xFF) return;

	newbyte = (unsigned char)(inbyte);
	switch (rpt->status)
	{
	    case TSIP_PARSED_DLE_1:
		switch (newbyte)
		{
		    case 0:
		    case ETX:
			/* illegal TSIP IDs */
			rpt->len = 0;
			rpt->status = TSIP_PARSED_EMPTY;
			break;
		    case DLE:
			/* try normal message start again */
			rpt->len = 0;
			rpt->status = TSIP_PARSED_DLE_1;
			break;
		    default:
			/* legal TSIP ID; start message */
			rpt->code = newbyte;
			rpt->len = 0;
			rpt->status = TSIP_PARSED_DATA;
			break;
		}
		break;
	    case TSIP_PARSED_DATA:
		switch (newbyte) {
		    case DLE:
			/* expect DLE or ETX next */
			rpt->status = TSIP_PARSED_DLE_2;
			break;
		    default:
			/* normal data byte  */
			rpt->buf[rpt->len] = newbyte;
			rpt->len++;
			/* no change in rpt->status */
			break;
		}
		break;
	    case TSIP_PARSED_DLE_2:
		switch (newbyte) {
		    case DLE:
			/* normal data byte */
			rpt->buf[rpt->len] = newbyte;
			rpt->len++;
			rpt->status = TSIP_PARSED_DATA;
			break;
		    case ETX:
			/* end of message; return TRUE here. */
			rpt->status = TSIP_PARSED_FULL;
			break;
		    default:
			/* error: treat as TSIP_PARSED_DLE_1; start new report packet */
			rpt->code = newbyte;
			rpt->len = 0;
			rpt->status = TSIP_PARSED_DATA;
		}
		break;
	    case TSIP_PARSED_FULL:
	    case TSIP_PARSED_EMPTY:
	    default:
		switch (newbyte) {
		    case DLE:
			/* normal message start */
			rpt->len = 0;
			rpt->status = TSIP_PARSED_DLE_1;
			break;
		    default:
			/* error: ignore newbyte */
			rpt->len = 0;
			rpt->status = TSIP_PARSED_EMPTY;
		}
		break;
	}
	if (rpt->len > MAX_RPTBUF) {
		/* error: start new report packet */
		rpt->status = TSIP_PARSED_EMPTY;
		rpt->len = 0;
	}
}

#ifdef TRIMBLE_OUTPUT_FUNC

/**/
/* Channel A configuration for dual port operation */
short
rpt_0x3D(
	 TSIPPKT *rpt,
	 unsigned char *tx_baud_index,
	 unsigned char *rx_baud_index,
	 unsigned char *char_format_index,
	 unsigned char *stop_bits,
	 unsigned char *tx_mode_index,
	 unsigned char *rx_mode_index
	 )
{
	unsigned char *buf;
	buf = rpt->buf;

	if (rpt->len != 6) return TRUE;
	*tx_baud_index = buf[0];
	*rx_baud_index = buf[1];
	*char_format_index = buf[2];
	*stop_bits = (unsigned char)((buf[3] == 0x07) ? 1 : 2);
	*tx_mode_index = buf[4];
	*rx_mode_index = buf[5];
	return FALSE;
}

/**/
/* almanac data for specified satellite */
short
rpt_0x40(
	 TSIPPKT *rpt,
	 unsigned char *sv_prn,
	 short *week_num,
	 float *t_zc,
	 float *eccentricity,
	 float *t_oa,
	 float *i_0,
	 float *OMEGA_dot,
	 float *sqrt_A,
	 float *OMEGA_0,
	 float *omega,
	 float *M_0
	 )
{
	unsigned char *buf;
	buf = rpt->buf;

	if (rpt->len != 39) return TRUE;
	*sv_prn = buf[0];
	*t_zc = bGetSingle (&buf[1]);
	*week_num = bGetShort (&buf[5]);
	*eccentricity = bGetSingle (&buf[7]);
	*t_oa = bGetSingle (&buf[11]);
	*i_0 = bGetSingle (&buf[15]);
	*OMEGA_dot = bGetSingle (&buf[19]);
	*sqrt_A = bGetSingle (&buf[23]);
	*OMEGA_0 = bGetSingle (&buf[27]);
	*omega = bGetSingle (&buf[31]);
	*M_0 = bGetSingle (&buf[35]);
	return FALSE;
}

/* GPS time */
short
rpt_0x41(
	 TSIPPKT *rpt,
	 float *time_of_week,
	 float *UTC_offset,
	 short *week_num
	 )
{
	unsigned char *buf;
	buf = rpt->buf;
	
	if (rpt->len != 10) return TRUE;
	*time_of_week = bGetSingle (buf);
	*week_num = bGetShort (&buf[4]);
	*UTC_offset = bGetSingle (&buf[6]);
	return FALSE;
}

/* position in ECEF, single precision */
short
rpt_0x42(
	 TSIPPKT *rpt,
	 float pos_ECEF[3],
	 float *time_of_fix
	 )
{
	unsigned char *buf;
	buf = rpt->buf;
	
	if (rpt->len != 16) return TRUE;
	pos_ECEF[0] = bGetSingle (buf);
	pos_ECEF[1]= bGetSingle (&buf[4]);
	pos_ECEF[2]= bGetSingle (&buf[8]);
	*time_of_fix = bGetSingle (&buf[12]);
	return FALSE;
}

/* velocity in ECEF, single precision */
short
rpt_0x43(
	 TSIPPKT *rpt,
	 float ECEF_vel[3],
	 float *freq_offset,
	 float *time_of_fix
	 )
{
	unsigned char *buf;
	buf = rpt->buf;

	if (rpt->len != 20) return TRUE;
	ECEF_vel[0] = bGetSingle (buf);
	ECEF_vel[1] = bGetSingle (&buf[4]);
	ECEF_vel[2] = bGetSingle (&buf[8]);
	*freq_offset = bGetSingle (&buf[12]);
	*time_of_fix = bGetSingle (&buf[16]);
	return FALSE;
}

/* software versions */	
short
rpt_0x45(
	 TSIPPKT *rpt,
	 unsigned char *major_nav_version,
	 unsigned char *minor_nav_version,
	 unsigned char *nav_day,
	 unsigned char *nav_month,
	 unsigned char *nav_year,
	 unsigned char *major_dsp_version,
	 unsigned char *minor_dsp_version,
	 unsigned char *dsp_day,
	 unsigned char *dsp_month,
	 unsigned char *dsp_year
	 )
{
	unsigned char *buf;
	buf = rpt->buf;

	if (rpt->len != 10) return TRUE;
	*major_nav_version = buf[0];
	*minor_nav_version = buf[1];
	*nav_day = buf[2];
	*nav_month = buf[3];
	*nav_year = buf[4];
	*major_dsp_version = buf[5];
	*minor_dsp_version = buf[6];
	*dsp_day = buf[7];
	*dsp_month = buf[8];
	*dsp_year = buf[9];
	return FALSE;
}

/* receiver health and status */
short
rpt_0x46(
	 TSIPPKT *rpt,
	 unsigned char *status1,
	 unsigned char *status2
	 )
{
	unsigned char *buf;
	buf = rpt->buf;

	if (rpt->len != 2) return TRUE;
	*status1 = buf[0];
	*status2 = buf[1];
	return FALSE;
}

/* signal levels for all satellites tracked */
short
rpt_0x47(
	 TSIPPKT *rpt,
	 unsigned char *nsvs,
	 unsigned char *sv_prn,
	 float *snr
	 )
{
	short isv;
	unsigned char *buf;
	buf = rpt->buf;

	if (rpt->len != 1 + 5*buf[0]) return TRUE;
	*nsvs = buf[0];
	for (isv = 0; isv < (*nsvs); isv++) {
		sv_prn[isv] = buf[5*isv + 1];
		snr[isv] = bGetSingle (&buf[5*isv + 2]);
	}
	return FALSE;
}

/* GPS system message */
short
rpt_0x48(
	 TSIPPKT *rpt,
	 unsigned char *message
	 )
{
	unsigned char *buf;
	buf = rpt->buf;

	if (rpt->len != 22) return TRUE;
	memcpy (message, buf, 22);
	message[22] = 0;
	return FALSE;
}

/* health for all satellites from almanac health page */
short
rpt_0x49(
	 TSIPPKT *rpt,
	 unsigned char *sv_health
	 )
{
	short i;
	unsigned char *buf;
	buf = rpt->buf;

	if (rpt->len != 32) return TRUE;
	for (i = 0; i < 32; i++) sv_health [i]= buf[i];
	return FALSE;
}

/* position in lat-lon-alt, single precision */
short
rpt_0x4A(
	 TSIPPKT *rpt,
	 float *lat,
	 float *lon,
	 float *alt,
	 float *clock_bias,
	 float *time_of_fix
	 )
{
	unsigned char *buf;
	buf = rpt->buf;

	if (rpt->len != 20) return TRUE;
	*lat = bGetSingle (buf);
	*lon = bGetSingle (&buf[4]);
	*alt = bGetSingle (&buf[8]);
	*clock_bias = bGetSingle (&buf[12]);
	*time_of_fix = bGetSingle (&buf[16]);
	return FALSE;
}

/* reference altitude parameters */
short
rpt_0x4A_2(
	   TSIPPKT *rpt,
	   float *alt,
	   float *dummy,
	   unsigned char *alt_flag
	   )
{
	unsigned char *buf;

	buf = rpt->buf;

	if (rpt->len != 9) return TRUE;
	*alt = bGetSingle (buf);
	*dummy = bGetSingle (&buf[4]);
	*alt_flag = buf[8];
	return FALSE;
}

/* machine ID code, status */
short
rpt_0x4B(
	 TSIPPKT *rpt,
	 unsigned char *machine_id,
	 unsigned char *status3,
	 unsigned char *status4
	 )
{
	unsigned char *buf;
	buf = rpt->buf;

	if (rpt->len != 3) return TRUE;
	*machine_id = buf[0];
	*status3 = buf[1];
	*status4 = buf[2];
	return FALSE;
}

/* operating parameters and masks */
short
rpt_0x4C(
	 TSIPPKT *rpt,
	 unsigned char *dyn_code,
	 float *el_mask,
	 float *snr_mask,
	 float *dop_mask,
	 float *dop_switch
	 )
{
	unsigned char *buf;
	buf = rpt->buf;

	if (rpt->len != 17) return TRUE;
	*dyn_code = buf[0];
	*el_mask = bGetSingle (&buf[1]);
	*snr_mask = bGetSingle (&buf[5]);
	*dop_mask = bGetSingle (&buf[9]);
	*dop_switch = bGetSingle (&buf[13]);
	return FALSE;
}

/* oscillator offset */
short
rpt_0x4D(
	 TSIPPKT *rpt,
	 float *osc_offset
	 )
{
	unsigned char *buf;
	buf = rpt->buf;

	if (rpt->len != 4) return TRUE;
	*osc_offset = bGetSingle (buf);
	return FALSE;
}

/* yes/no response to command to set GPS time */
short
rpt_0x4E(
	 TSIPPKT *rpt,
	 unsigned char *response
	 )
{
	unsigned char *buf;
	buf = rpt->buf;

	if (rpt->len != 1) return TRUE;
	*response = buf[0];
	return FALSE;
}

/* UTC data */
short
rpt_0x4F(
	 TSIPPKT *rpt,
	 double *a0,
	 float *a1,
	 float *time_of_data,
	 short *dt_ls,
	 short *wn_t,
	 short *wn_lsf,
	 short *dn,
	 short *dt_lsf
	 )
{
	unsigned char *buf;
	buf = rpt->buf;

	if (rpt->len != 26) return TRUE;
	*a0 = bGetDouble (buf);
	*a1 = bGetSingle (&buf[8]);
	*dt_ls = bGetShort (&buf[12]);
	*time_of_data = bGetSingle (&buf[14]);
	*wn_t = bGetShort (&buf[18]);
	*wn_lsf = bGetShort (&buf[20]);
	*dn = bGetShort (&buf[22]);
	*dt_lsf = bGetShort (&buf[24]);
	return FALSE;
}

/**/
/* clock offset and frequency offset in 1-SV (0-D) mode */
short
rpt_0x54(
	 TSIPPKT *rpt,
	 float *clock_bias,
	 float *freq_offset,
	 float *time_of_fix
	 )
{
	unsigned char *buf;
	buf = rpt->buf;

	if (rpt->len != 12) return TRUE;
	*clock_bias = bGetSingle (buf);
	*freq_offset = bGetSingle (&buf[4]);
	*time_of_fix = bGetSingle (&buf[8]);
	return FALSE;
}

/* I/O serial options */
short
rpt_0x55(
	 TSIPPKT *rpt,
	 unsigned char *pos_code,
	 unsigned char *vel_code,
	 unsigned char *time_code,
	 unsigned char *aux_code
	 )
{
	unsigned char *buf;
	buf = rpt->buf;
	
	if (rpt->len != 4) return TRUE;
	*pos_code = buf[0];
	*vel_code = buf[1];
	*time_code = buf[2];
	*aux_code = buf[3];
	return FALSE;
}

/* velocity in east-north-up coordinates */	
short
rpt_0x56(
	 TSIPPKT *rpt,
	 float vel_ENU[3],
	 float *freq_offset,
	 float *time_of_fix
	 )
{
	unsigned char *buf;
	buf = rpt->buf;
	
	if (rpt->len != 20) return TRUE;
	/* east */
	vel_ENU[0] = bGetSingle (buf);
	/* north */
	vel_ENU[1] = bGetSingle (&buf[4]);
	/* up */
	vel_ENU[2] = bGetSingle (&buf[8]);
	*freq_offset = bGetSingle (&buf[12]);
	*time_of_fix = bGetSingle (&buf[16]);
	return FALSE;
}

/* info about last computed fix */
short
rpt_0x57(
	 TSIPPKT *rpt,
	 unsigned char *source_code,
	 unsigned char *diag_code,
	 short *week_num,
	 float *time_of_fix
	 )
{
	unsigned char *buf;
	buf = rpt->buf;
	
	if (rpt->len != 8) return TRUE;
	*source_code = buf[0];
	*diag_code = buf[1];
	*time_of_fix = bGetSingle (&buf[2]);
	*week_num = bGetShort (&buf[6]);
	return FALSE;
}

/* GPS system data or acknowledgment of GPS system data load */
short
rpt_0x58(
	 TSIPPKT *rpt,
	 unsigned char *op_code,
	 unsigned char *data_type,
	 unsigned char *sv_prn,
	 unsigned char *data_length,
	 unsigned char *data_packet
	 )
{
	unsigned char *buf, *buf4;
	short dl;
	ALM_INFO* alminfo;
	ION_INFO* ioninfo;
	UTC_INFO* utcinfo;
	NAV_INFO* navinfo;

	buf = rpt->buf;

	if (buf[0] == 2) {
		if (rpt->len < 4) return TRUE;
		if (rpt->len != 4+buf[3]) return TRUE;
	}
	else if (rpt->len != 3) {
		return TRUE;
	}
	*op_code = buf[0];
	*data_type = buf[1];
	*sv_prn = buf[2];
	if (*op_code == 2) {
		dl = buf[3];
		*data_length = (unsigned char)dl;
		buf4 = &buf[4];
		switch (*data_type) {
		    case 2:
			/* Almanac */
			if (*data_length != sizeof (ALM_INFO)) return TRUE;
			alminfo = (ALM_INFO*)data_packet;
			alminfo->t_oa_raw  = buf4[0];
			alminfo->SV_health = buf4[1];
			alminfo->e         = bGetSingle(&buf4[2]);
			alminfo->t_oa      = bGetSingle(&buf4[6]);
			alminfo->i_0       = bGetSingle(&buf4[10]);
			alminfo->OMEGADOT  = bGetSingle(&buf4[14]);
			alminfo->sqrt_A    = bGetSingle(&buf4[18]);
			alminfo->OMEGA_0   = bGetSingle(&buf4[22]);
			alminfo->omega     = bGetSingle(&buf4[26]);
			alminfo->M_0       = bGetSingle(&buf4[30]);
			alminfo->a_f0      = bGetSingle(&buf4[34]);
			alminfo->a_f1      = bGetSingle(&buf4[38]);
			alminfo->Axis      = bGetSingle(&buf4[42]);
			alminfo->n         = bGetSingle(&buf4[46]);
			alminfo->OMEGA_n   = bGetSingle(&buf4[50]);
			alminfo->ODOT_n    = bGetSingle(&buf4[54]);
			alminfo->t_zc      = bGetSingle(&buf4[58]);
			alminfo->weeknum   = bGetShort(&buf4[62]);
			alminfo->wn_oa     = bGetShort(&buf4[64]);
			break;

		    case 3:
			/* Almanac health page */
			if (*data_length != sizeof (ALH_PARMS) + 3) return TRUE;

			/* this record is returned raw */
			memcpy (data_packet, buf4, dl);
			break;

		    case 4:
			/* Ionosphere */
			if (*data_length != sizeof (ION_INFO) + 8) return TRUE;
			ioninfo = (ION_INFO*)data_packet;
			ioninfo->alpha_0   = bGetSingle (&buf4[8]);
			ioninfo->alpha_1   = bGetSingle (&buf4[12]);
			ioninfo->alpha_2   = bGetSingle (&buf4[16]);
			ioninfo->alpha_3   = bGetSingle (&buf4[20]);
			ioninfo->beta_0    = bGetSingle (&buf4[24]);
			ioninfo->beta_1    = bGetSingle (&buf4[28]);
			ioninfo->beta_2    = bGetSingle (&buf4[32]);
			ioninfo->beta_3    = bGetSingle (&buf4[36]);
			break;

		    case 5:
			/* UTC */
			if (*data_length != sizeof (UTC_INFO) + 13) return TRUE;
			utcinfo = (UTC_INFO*)data_packet;
			utcinfo->A_0       = bGetDouble (&buf4[13]);
			utcinfo->A_1       = bGetSingle (&buf4[21]);
			utcinfo->delta_t_LS = bGetShort (&buf4[25]);
			utcinfo->t_ot      = bGetSingle(&buf4[27]);
			utcinfo->WN_t      = bGetShort (&buf4[31]);
			utcinfo->WN_LSF    = bGetShort (&buf4[33]);
			utcinfo->DN        = bGetShort (&buf4[35]);
			utcinfo->delta_t_LSF = bGetShort (&buf4[37]);
			break;

		    case 6:
			/* Ephemeris */
			if (*data_length != sizeof (NAV_INFO) - 1) return TRUE;

			navinfo = (NAV_INFO*)data_packet;

			navinfo->sv_number = buf4[0];
			navinfo->t_ephem = bGetSingle (&buf4[1]);
			navinfo->ephclk.weeknum = bGetShort (&buf4[5]);

			navinfo->ephclk.codeL2 = buf4[7];
			navinfo->ephclk.L2Pdata = buf4[8];
			navinfo->ephclk.SVacc_raw = buf4[9];
			navinfo->ephclk.SV_health = buf4[10];
			navinfo->ephclk.IODC = bGetShort (&buf4[11]);
			navinfo->ephclk.T_GD = bGetSingle (&buf4[13]);
			navinfo->ephclk.t_oc = bGetSingle (&buf4[17]);
			navinfo->ephclk.a_f2 = bGetSingle (&buf4[21]);
			navinfo->ephclk.a_f1 = bGetSingle (&buf4[25]);
			navinfo->ephclk.a_f0 = bGetSingle (&buf4[29]);
			navinfo->ephclk.SVacc = bGetSingle (&buf4[33]);

			navinfo->ephorb.IODE = buf4[37];
			navinfo->ephorb.fit_interval = buf4[38];
			navinfo->ephorb.C_rs = bGetSingle (&buf4[39]);
			navinfo->ephorb.delta_n = bGetSingle (&buf4[43]);
			navinfo->ephorb.M_0 = bGetDouble (&buf4[47]);
			navinfo->ephorb.C_uc = bGetSingle (&buf4[55]);
			navinfo->ephorb.e = bGetDouble (&buf4[59]);
			navinfo->ephorb.C_us = bGetSingle (&buf4[67]);
			navinfo->ephorb.sqrt_A = bGetDouble (&buf4[71]);
			navinfo->ephorb.t_oe = bGetSingle (&buf4[79]);
			navinfo->ephorb.C_ic = bGetSingle (&buf4[83]);
			navinfo->ephorb.OMEGA_0 = bGetDouble (&buf4[87]);
			navinfo->ephorb.C_is = bGetSingle (&buf4[95]);
			navinfo->ephorb.i_0 = bGetDouble (&buf4[99]);
			navinfo->ephorb.C_rc = bGetSingle (&buf4[107]);
			navinfo->ephorb.omega = bGetDouble (&buf4[111]);
			navinfo->ephorb.OMEGADOT=bGetSingle (&buf4[119]);
			navinfo->ephorb.IDOT = bGetSingle (&buf4[123]);
			navinfo->ephorb.Axis = bGetDouble (&buf4[127]);
			navinfo->ephorb.n = bGetDouble (&buf4[135]);
			navinfo->ephorb.r1me2 = bGetDouble (&buf4[143]);
			navinfo->ephorb.OMEGA_n=bGetDouble (&buf4[151]);
			navinfo->ephorb.ODOT_n = bGetDouble (&buf4[159]);
			break;
		}
	}
	return FALSE;
}

/* satellite enable/disable or health heed/ignore list */	
short
rpt_0x59(
	 TSIPPKT *rpt,
	 unsigned char *code_type,
	 unsigned char status_code[32]
	 )
{
	short iprn;
	unsigned char *buf;
	buf = rpt->buf;
	
	if (rpt->len != 33) return TRUE;
	*code_type = buf[0];
	for (iprn = 0; iprn < 32; iprn++)
		status_code[iprn] = buf[iprn + 1];
	return FALSE;
}

/* raw measurement data - code phase/Doppler */
short
rpt_0x5A(
	 TSIPPKT *rpt,
	 unsigned char *sv_prn,
	 float *sample_length,
	 float *signal_level,
	 float *code_phase,
	 float *Doppler,
	 double *time_of_fix
	 )
{
	unsigned char *buf;
	buf = rpt->buf;

	if (rpt->len != 25) return TRUE;
	*sv_prn = buf[0];
	*sample_length = bGetSingle (&buf[1]);
	*signal_level = bGetSingle (&buf[5]);
	*code_phase = bGetSingle (&buf[9]);
	*Doppler = bGetSingle (&buf[13]);
	*time_of_fix = bGetDouble (&buf[17]);
	return FALSE;
}

/* satellite ephorb status */	
short
rpt_0x5B(
	 TSIPPKT *rpt,
	 unsigned char *sv_prn,
	 unsigned char *sv_health,
	 unsigned char *sv_iode,
	 unsigned char *fit_interval_flag,
	 float *time_of_collection,
	 float *time_of_eph,
	 float *sv_accy
	 )
{
	unsigned char *buf;
	buf = rpt->buf;
	
	if (rpt->len != 16) return TRUE;
	*sv_prn = buf[0];
	*time_of_collection = bGetSingle (&buf[1]);
	*sv_health = buf[5];
	*sv_iode = buf[6];
	*time_of_eph = bGetSingle (&buf[7]);
	*fit_interval_flag = buf[11];
	*sv_accy = bGetSingle (&buf[12]);
	return FALSE;
}

/* satellite tracking status */
short
rpt_0x5C(
	 TSIPPKT *rpt,
	 unsigned char *sv_prn,
	 unsigned char *slot,
	 unsigned char *chan,
	 unsigned char *acq_flag,
	 unsigned char *eph_flag,
	 float *signal_level,
	 float *time_of_last_msmt,
	 float *elev,
	 float *azim,
	 unsigned char *old_msmt_flag,
	 unsigned char *integer_msec_flag,
	 unsigned char *bad_data_flag,
	 unsigned char *data_collect_flag
	 )
{
	unsigned char *buf;
	buf = rpt->buf;
	
	if (rpt->len != 24) return TRUE;
	*sv_prn = buf[0];
	*slot = (unsigned char)((buf[1] & 0x07) + 1);
	*chan = (unsigned char)(buf[1] >> 3);
	if (*chan == 0x10) *chan = 2;
	else (*chan)++;
	*acq_flag = buf[2];
	*eph_flag = buf[3];
	*signal_level = bGetSingle (&buf[4]);
	*time_of_last_msmt = bGetSingle (&buf[8]);
	*elev = bGetSingle (&buf[12]);
	*azim = bGetSingle (&buf[16]);
	*old_msmt_flag = buf[20];
	*integer_msec_flag = buf[21];
	*bad_data_flag = buf[22];
	*data_collect_flag = buf[23];
	return FALSE;
}

/**/
/* over-determined satellite selection for position fixes, PDOP, fix mode */
short
rpt_0x6D(
	 TSIPPKT *rpt,
	 unsigned char *manual_mode,
	 unsigned char *nsvs,
	 unsigned char *ndim,
	 unsigned char sv_prn[],
	 float *pdop,
	 float *hdop,
	 float *vdop,
	 float *tdop
	 )
{
	short islot;
	unsigned char *buf;
	buf = rpt->buf;

	*nsvs = (unsigned char)((buf[0] & 0xF0) >> 4);
	if ((*nsvs)>8) return TRUE;
	if (rpt->len != 17 + (*nsvs) ) return TRUE;

	*manual_mode = (unsigned char)(buf[0] & 0x08);
	*ndim  = (unsigned char)((buf[0] & 0x07));
	*pdop = bGetSingle (&buf[1]);
	*hdop = bGetSingle (&buf[5]);
	*vdop = bGetSingle (&buf[9]);
	*tdop = bGetSingle (&buf[13]);
	for (islot = 0; islot < (*nsvs); islot++)
		sv_prn[islot] = buf[islot + 17];
	return FALSE;
}

/**/
/* differential fix mode */
short
rpt_0x82(
	 TSIPPKT *rpt,
	 unsigned char *diff_mode
	 )
{
	unsigned char *buf;
	buf = rpt->buf;

	if (rpt->len != 1) return TRUE;
	*diff_mode = buf[0];
	return FALSE;
}

/* position, ECEF double precision */
short
rpt_0x83(
	 TSIPPKT *rpt,
	 double ECEF_pos[3],
	 double *clock_bias,
	 float *time_of_fix
	 )
{
	unsigned char *buf;
	buf = rpt->buf;

	if (rpt->len != 36) return TRUE;
	ECEF_pos[0] = bGetDouble (buf);
	ECEF_pos[1] = bGetDouble (&buf[8]);
	ECEF_pos[2] = bGetDouble (&buf[16]);
	*clock_bias  = bGetDouble (&buf[24]);
	*time_of_fix = bGetSingle (&buf[32]);
	return FALSE;
}

/* position, lat-lon-alt double precision */	
short
rpt_0x84(
	 TSIPPKT *rpt,
	 double *lat,
	 double *lon,
	 double *alt,
	 double *clock_bias,
	 float *time_of_fix
	 )
{
	unsigned char *buf;
	buf = rpt->buf;

	if (rpt->len != 36) return TRUE;
	*lat = bGetDouble (buf);
	*lon = bGetDouble (&buf[8]);
	*alt = bGetDouble (&buf[16]);
	*clock_bias = bGetDouble (&buf[24]);
	*time_of_fix = bGetSingle (&buf[32]);
	return FALSE;
}

short
rpt_Paly0xBB(
	     TSIPPKT *rpt,
	     TSIP_RCVR_CFG *TsipxBB
	     )
{
	unsigned char *buf;
	buf = rpt->buf;

	/* Palisade is inconsistent with other TSIP, which has a length of 40 */
	/* if (rpt->len != 40) return TRUE; */
	if (rpt->len != 43) return TRUE;

	TsipxBB->bSubcode	=  buf[0];
	TsipxBB->operating_mode	=  buf[1];
	TsipxBB->dyn_code	=  buf[3];
	TsipxBB->elev_mask	=  bGetSingle (&buf[5]);
	TsipxBB->cno_mask	=  bGetSingle (&buf[9]);
	TsipxBB->dop_mask 	=  bGetSingle (&buf[13]);
	TsipxBB->dop_switch 	=  bGetSingle (&buf[17]);
	return FALSE;
}

/* Receiver serial port configuration */
short
rpt_0xBC(
	 TSIPPKT *rpt,
	 unsigned char *port_num,
	 unsigned char *in_baud,
	 unsigned char *out_baud,
	 unsigned char *data_bits,
	 unsigned char *parity,
	 unsigned char *stop_bits,
	 unsigned char *flow_control,
	 unsigned char *protocols_in,
	 unsigned char *protocols_out,
	 unsigned char *reserved
	 )
{
	unsigned char *buf;
	buf = rpt->buf;

	if (rpt->len != 10) return TRUE;
	*port_num = buf[0];
	*in_baud = buf[1];
	*out_baud = buf[2];
	*data_bits = buf[3];
	*parity = buf[4];
	*stop_bits = buf[5];
	*flow_control = buf[6];
	*protocols_in = buf[7];
	*protocols_out = buf[8];
	*reserved = buf[9];

	return FALSE;
}

/**** Superpackets ****/

short
rpt_0x8F0B(
	   TSIPPKT *rpt,
	   unsigned short *event,
	   double *tow,
	   unsigned char *date,
	   unsigned char *month,
	   short *year,
	   unsigned char *dim_mode,
	   short *utc_offset,
	   double *bias,
	   double *drift,
	   float *bias_unc,
	   float *dr_unc,
	   double *lat,
	   double *lon,
	   double *alt,
	   char sv_id[8]
	   )
{
	short local_index;
	unsigned char *buf;

	buf = rpt->buf;
	if (rpt->len != 74) return TRUE;
	*event = bGetShort(&buf[1]);
	*tow = bGetDouble(&buf[3]);
	*date = buf[11];
	*month = buf[12];
	*year = bGetShort(&buf[13]);
	*dim_mode = buf[15];
	*utc_offset = bGetShort(&buf[16]);
	*bias = bGetDouble(&buf[18]);
	*drift = bGetDouble(&buf[26]);
	*bias_unc = bGetSingle(&buf[34]);
	*dr_unc = bGetSingle(&buf[38]);
	*lat = bGetDouble(&buf[42]);
	*lon = bGetDouble(&buf[50]);
	*alt = bGetDouble(&buf[58]);

	for (local_index=0; local_index<8; local_index++) sv_id[local_index] = buf[local_index + 66];
	return FALSE;
}

/* datum index and coefficients  */
short
rpt_0x8F14(
	   TSIPPKT *rpt,
	   short *datum_idx,
	   double datum_coeffs[5]
	   )
{
	unsigned char *buf;
	buf = rpt->buf;

	if (rpt->len != 43) return TRUE;
	*datum_idx = bGetShort(&buf[1]);
	datum_coeffs[0] = bGetDouble (&buf[3]);
	datum_coeffs[1] = bGetDouble (&buf[11]);
	datum_coeffs[2] = bGetDouble (&buf[19]);
	datum_coeffs[3] = bGetDouble (&buf[27]);
	datum_coeffs[4] = bGetDouble (&buf[35]);
	return FALSE;
}


/* datum index and coefficients  */
short
rpt_0x8F15(
	   TSIPPKT *rpt,
	   short *datum_idx,
	   double datum_coeffs[5]
	   )
{
	unsigned char *buf;
	buf = rpt->buf;

	if (rpt->len != 43) return TRUE;
	*datum_idx = bGetShort(&buf[1]);
	datum_coeffs[0] = bGetDouble (&buf[3]);
	datum_coeffs[1] = bGetDouble (&buf[11]);
	datum_coeffs[2] = bGetDouble (&buf[19]);
	datum_coeffs[3] = bGetDouble (&buf[27]);
	datum_coeffs[4] = bGetDouble (&buf[35]);
	return FALSE;
}


#define MAX_LONG  (2147483648.)   /* 2**31 */

short
rpt_0x8F20(
	   TSIPPKT *rpt,
	   unsigned char *info,
	   double *lat,
	   double *lon,
	   double *alt,
	   double vel_enu[],
	   double *time_of_fix,
	   short *week_num,
	   unsigned char *nsvs,
	   unsigned char sv_prn[],
	   short sv_IODC[],
	   short *datum_index
	   )
{
	short
	    isv;
	unsigned char
	    *buf, prnx, iode;
	unsigned long
	    ulongtemp;
	long
	    longtemp;
	double
	    vel_scale;

	buf = rpt->buf;

	if (rpt->len != 56) return TRUE;

	vel_scale = (buf[24]&1)? 0.020 : 0.005;
	vel_enu[0] = bGetShort (buf+2)*vel_scale;
	vel_enu[1] = bGetShort (buf+4)*vel_scale;
	vel_enu[2] = bGetShort (buf+6)*vel_scale;

	*time_of_fix = bGetULong (buf+8)*.001;

	longtemp = bGetLong (buf+12);
	*lat = longtemp*(GPS_PI/MAX_LONG);

	ulongtemp = bGetULong (buf+16);
	*lon = ulongtemp*(GPS_PI/MAX_LONG);
	if (*lon > GPS_PI) *lon -= 2.0*GPS_PI;

	*alt = bGetLong (buf+20)*.001;
	/* 25 blank; 29 = UTC */
	(*datum_index) = (short)((short)buf[26]-1);
	*info = buf[27];
	*nsvs = buf[28];
	*week_num = bGetShort (&buf[30]);
	for (isv = 0; isv < 8; isv++) {
		prnx = buf[32+2*isv];
		sv_prn[isv] = (unsigned char)(prnx&0x3F);
		iode = buf[33+2*isv];
		sv_IODC[isv] = (short)(iode | ((prnx>>6)<<8));
	}
	return FALSE;
}

short
rpt_0x8F41(
	   TSIPPKT *rpt,
	   unsigned char *bSearchRange,
	   unsigned char *bBoardOptions,
	   unsigned long *iiSerialNumber,
	   unsigned char *bBuildYear,
	   unsigned char *bBuildMonth,
	   unsigned char *bBuildDay,
	   unsigned char *bBuildHour,
	   float *fOscOffset,
	   unsigned short *iTestCodeId
	   )
{
	if (rpt->len != 17) return FALSE;
	*bSearchRange = rpt->buf[1];
	*bBoardOptions = rpt->buf[2];
	*iiSerialNumber = bGetLong(&rpt->buf[3]);
	*bBuildYear = rpt->buf[7];
	*bBuildMonth = rpt->buf[8];
	*bBuildDay = rpt->buf[9];
	*bBuildHour =	rpt->buf[10];
	*fOscOffset = bGetSingle(&rpt->buf[11]);
	*iTestCodeId = bGetShort(&rpt->buf[15]);
/*	Tsipx8E41Data = *Tsipx8E41; */
	return TRUE;
}

short
rpt_0x8F42(
	   TSIPPKT *rpt,
	   unsigned char *bProdOptionsPre,
	   unsigned char *bProdNumberExt,
	   unsigned short *iCaseSerialNumberPre,
	   unsigned long *iiCaseSerialNumber,
	   unsigned long *iiProdNumber,
	   unsigned short *iPremiumOptions,
	   unsigned short *iMachineID,
	   unsigned short *iKey
	   )
{
	if (rpt->len != 19) return FALSE;
	*bProdOptionsPre = rpt->buf[1];
	*bProdNumberExt = rpt->buf[2];
	*iCaseSerialNumberPre = bGetShort(&rpt->buf[3]);
	*iiCaseSerialNumber = bGetLong(&rpt->buf[5]);
	*iiProdNumber = bGetLong(&rpt->buf[9]);
	*iPremiumOptions = bGetShort(&rpt->buf[13]);
	*iMachineID = bGetShort(&rpt->buf[15]);
	*iKey = bGetShort(&rpt->buf[17]);
	return TRUE;
}

short
rpt_0x8F45(
	   TSIPPKT *rpt,
	   unsigned char *bSegMask
	   )
{
	if (rpt->len != 2) return FALSE;
	*bSegMask = rpt->buf[1];
	return TRUE;
}

/* Stinger PPS definition */
short
rpt_0x8F4A_16(
	      TSIPPKT *rpt,
	      unsigned char *pps_enabled,
	      unsigned char *pps_timebase,
	      unsigned char *pos_polarity,
	      double *pps_offset,
	      float *bias_unc_threshold
	      )
{
	unsigned char
	    *buf;

	buf = rpt->buf;
	if (rpt->len != 16) return TRUE;
	*pps_enabled = buf[1];
	*pps_timebase = buf[2];
	*pos_polarity = buf[3];
	*pps_offset = bGetDouble(&buf[4]);
	*bias_unc_threshold = bGetSingle(&buf[12]);
	return FALSE;
}

short
rpt_0x8F4B(
	   TSIPPKT *rpt,
	   unsigned long *decorr_max
	   )
{
	unsigned char
	    *buf;

	buf = rpt->buf;
	if (rpt->len != 5) return TRUE;
	*decorr_max = bGetLong(&buf[1]);
	return FALSE;
}

short
rpt_0x8F4D(
	   TSIPPKT *rpt,
	   unsigned long *event_mask
	   )
{
	unsigned char
	    *buf;

	buf = rpt->buf;
	if (rpt->len != 5) return TRUE;
	*event_mask = bGetULong (&buf[1]);
	return FALSE;
}

short
rpt_0x8FA5(
	   TSIPPKT *rpt,
	   unsigned char *spktmask
	   )
{
	unsigned char
	    *buf;

	buf = rpt->buf;
	if (rpt->len != 5) return TRUE;
	spktmask[0] = buf[1];
	spktmask[1] = buf[2];
	spktmask[2] = buf[3];
	spktmask[3] = buf[4];
	return FALSE;
}

short
rpt_0x8FAD(
	   TSIPPKT *rpt,
	   unsigned short *COUNT,
	   double *FracSec,
	   unsigned char *Hour,
	   unsigned char *Minute,
	   unsigned char *Second,
	   unsigned char *Day,
	   unsigned char *Month,
	   unsigned short *Year,
	   unsigned char *Status,
	   unsigned char *Flags
	   )
{
	if (rpt->len != 22) return TRUE;

	*COUNT = bGetUShort(&rpt->buf[1]);
	*FracSec = bGetDouble(&rpt->buf[3]);
	*Hour = rpt->buf[11];
	*Minute = rpt->buf[12];
	*Second = rpt->buf[13];
	*Day = rpt->buf[14];
	*Month = rpt->buf[15];
	*Year = bGetUShort(&rpt->buf[16]);
	*Status = rpt->buf[18];
	*Flags = rpt->buf[19];
	return FALSE;
}


/*
 * *************************************************************************
 *
 * Trimble Navigation, Ltd.
 * OEM Products Development Group
 * P.O. Box 3642
 * 645 North Mary Avenue
 * Sunnyvale, California 94088-3642
 *
 * Corporate Headquarter:
 *    Telephone:  (408) 481-8000
 *    Fax:        (408) 481-6005
 *
 * Technical Support Center:
 *    Telephone:  (800) 767-4822	(U.S. and Canada)
 *                (408) 481-6940    (outside U.S. and Canada)
 *    Fax:        (408) 481-6020
 *    BBS:        (408) 481-7800
 *    e-mail:     trimble_support@trimble.com
 *		ftp://ftp.trimble.com/pub/sct/embedded/bin
 *
 * *************************************************************************
 *
 * T_REPORT.C consists of a primary function TranslateTSIPReportToText()
 * called by main().
 *
 * This function takes a character buffer that has been received as a report
 * from a TSIP device and interprets it.  The character buffer has been
 * assembled using tsip_input_proc() in T_PARSER.C.
 *
 * A large case statement directs processing to one of many mid-level
 * functions.  The mid-level functions specific to the current report
 * code passes the report buffer to the appropriate report decoder
 * rpt_0x?? () in T_PARSER.C, which converts the byte stream in rpt.buf
 * to data values approporaite for use.
 *
 * *************************************************************************
 *
 */


#define GOOD_PARSE 0
#define BADID_PARSE 1
#define BADLEN_PARSE 2
#define BADDATA_PARSE 3

#define B_TSIP	0x02
#define B_NMEA	0x04


/* pbuf is the pointer to the current location of the text output */
static char
*pbuf;

/* keep track of whether the message has been successfully parsed */
static short
parsed;


/* convert time of week into day-hour-minute-second and print */
char *
show_time(
	  float time_of_week
	  )
{
	short	days, hours, minutes;
	float seconds;
	double tow = 0;
	static char timestring [80];

	if (time_of_week == -1.0)
	{
		sprintf(timestring, "   <No time yet>   ");
	}
	else if ((time_of_week >= 604800.0) || (time_of_week < 0.0))
	{
		sprintf(timestring, "     <Bad time>     ");
	}
	else
	{
		if (time_of_week < 604799.9) 
			tow = time_of_week + .00000001;
		seconds = (float)fmod(tow, 60.);
		minutes =  (short) fmod(tow/60., 60.);
		hours = (short)fmod(tow / 3600., 24.);
		days = (short)(tow / 86400.0);
		sprintf(timestring, " %s %02d:%02d:%05.2f   ",
			dayname[days], hours, minutes, seconds);
	}
	return timestring;
}

/**/
/* 0x3D */
static void
rpt_chan_A_config(
		  TSIPPKT *rpt
		  )
{
	unsigned char
	    tx_baud_index, rx_baud_index,
	    char_format_index, stop_bits,
	    tx_mode_index, rx_mode_index,
	    databits, parity;
	int
	    i, nbaud;

	/* unload rptbuf */
	if (rpt_0x3D (rpt,
		      &tx_baud_index, &rx_baud_index, &char_format_index,
		      &stop_bits, &tx_mode_index, &rx_mode_index)) {
		parsed = BADLEN_PARSE;
		return;
	}

	pbuf += sprintf(pbuf, "\nChannel A Configuration");

	nbaud = sizeof(old_baudnum);

	for (i = 0; i < nbaud; ++i) if (tx_baud_index == old_baudnum[i]) break;
	pbuf += sprintf(pbuf, "\n   Transmit speed: %s at %s",
			old_output_ch[tx_mode_index], st_baud_text_app[i]);

	for (i = 0; i < nbaud; ++i) if (rx_baud_index == old_baudnum[i]) break;
	pbuf += sprintf(pbuf, "\n   Receive speed: %s at %s",
			old_input_ch[rx_mode_index], st_baud_text_app[i]);

	databits = (unsigned char)((char_format_index & 0x03) + 5);

	parity = (unsigned char)(char_format_index >> 2);
	if (parity > 4) parity = 2;

	pbuf += sprintf(pbuf, "\n   Character format (bits/char, parity, stop bits): %d-%s-%d",
			databits, old_parity_text[parity], stop_bits);
}

/**/
/* 0x40 */
static void
rpt_almanac_data_page(
		      TSIPPKT *rpt
		      )
{
	unsigned char
	    sv_prn;
	short
	    week_num;
	float
	    t_zc,
	    eccentricity,
	    t_oa,
	    i_0,
	    OMEGA_dot,
	    sqrt_A,
	    OMEGA_0,
	    omega,
	    M_0;

	/* unload rptbuf */
	if (rpt_0x40 (rpt,
		      &sv_prn, &week_num, &t_zc, &eccentricity, &t_oa,
		      &i_0, &OMEGA_dot, &sqrt_A, &OMEGA_0, &omega, &M_0)) {
		parsed = BADLEN_PARSE;
		return;
	}

	pbuf += sprintf(pbuf, "\nAlmanac for SV %02d", sv_prn);
	pbuf += sprintf(pbuf, "\n       Captured:%15.0f %s",
			t_zc, show_time (t_zc));
	pbuf += sprintf(pbuf, "\n           week:%15d", week_num);
	pbuf += sprintf(pbuf, "\n   Eccentricity:%15g", eccentricity);
	pbuf += sprintf(pbuf, "\n           T_oa:%15.0f %s",
			t_oa, show_time (t_oa));
	pbuf += sprintf(pbuf, "\n            i 0:%15g", i_0);
	pbuf += sprintf(pbuf, "\n      OMEGA dot:%15g", OMEGA_dot);
	pbuf += sprintf(pbuf, "\n         sqrt A:%15g", sqrt_A);
	pbuf += sprintf(pbuf, "\n        OMEGA 0:%15g", OMEGA_0);
	pbuf += sprintf(pbuf, "\n          omega:%15g", omega);
	pbuf += sprintf(pbuf, "\n            M 0:%15g", M_0);
}

/* 0x41 */
static void
rpt_GPS_time(
	     TSIPPKT *rpt
	     )
{
	float
	    time_of_week, UTC_offset;
	short
	    week_num;

	/* unload rptbuf */
	if (rpt_0x41 (rpt, &time_of_week, &UTC_offset, &week_num)) {
		parsed = BADLEN_PARSE;
		return;
	}

	pbuf += sprintf(pbuf, "\nGPS time:%s GPS week: %d   UTC offset %.1f",
			show_time(time_of_week), week_num, UTC_offset);

}

/* 0x42 */
static void
rpt_single_ECEF_position(
			 TSIPPKT *rpt
			 )
{
	float
	    ECEF_pos[3], time_of_fix;

	/* unload rptbuf */
	if (rpt_0x42 (rpt, ECEF_pos, &time_of_fix)) {
		parsed = BADLEN_PARSE;
		return;
	}

	pbuf += sprintf(pbuf, "\nSXYZ:  %15.0f  %15.0f  %15.0f    %s",
			ECEF_pos[0], ECEF_pos[1], ECEF_pos[2],
			show_time(time_of_fix));
}

/* 0x43 */
static void
rpt_single_ECEF_velocity(
			 TSIPPKT *rpt
			 )
{

	float
	    ECEF_vel[3], freq_offset, time_of_fix;

	/* unload rptbuf */
	if (rpt_0x43 (rpt, ECEF_vel, &freq_offset, &time_of_fix)) {
		parsed = BADLEN_PARSE;
		return;
	}

	pbuf += sprintf(pbuf, "\nVelECEF: %11.3f  %11.3f  %11.3f  %12.3f%s",
			ECEF_vel[0], ECEF_vel[1], ECEF_vel[2], freq_offset,
			show_time(time_of_fix));
}

/*  0x45  */
static void
rpt_SW_version(
	       TSIPPKT *rpt
	       )
{
	unsigned char
	    major_nav_version, minor_nav_version,
	    nav_day, nav_month, nav_year,
	    major_dsp_version, minor_dsp_version,
	    dsp_day, dsp_month, dsp_year;

	/* unload rptbuf */
	if (rpt_0x45 (rpt,
		      &major_nav_version, &minor_nav_version,
		      &nav_day, &nav_month, &nav_year,
		      &major_dsp_version, &minor_dsp_version,
		      &dsp_day, &dsp_month, &dsp_year)) {
		parsed = BADLEN_PARSE;
		return;
	}

	pbuf += sprintf(pbuf,
			"\nFW Versions:  Nav Proc %2d.%02d  %2d/%2d/%2d  Sig Proc %2d.%02d  %2d/%2d/%2d",
			major_nav_version, minor_nav_version, nav_day, nav_month, nav_year,
			major_dsp_version, minor_dsp_version, dsp_day, dsp_month, dsp_year);
}

/* 0x46 */
static void
rpt_rcvr_health(
		TSIPPKT *rpt
		)
{
	unsigned char
	    status1, status2;
	const char
	    *text;
	static const char const
	    *sc_text[] = {
		"Doing position fixes",
		"Don't have GPS time yet",
		"Waiting for almanac collection",
		"DOP too high          ",
		"No satellites available",
		"Only 1 satellite available",
		"Only 2 satellites available",
		"Only 3 satellites available",
		"No satellites usable   ",
		"Only 1 satellite usable",
		"Only 2 satellites usable",
		"Only 3 satellites usable",
		"Chosen satellite unusable"};


	/* unload rptbuf */
	if (rpt_0x46 (rpt, &status1, &status2))
	{
		parsed = BADLEN_PARSE;
		return;
	}

	text = (status1 < COUNTOF(sc_text))
	    ? sc_text[status1]
	    : "(out of range)";
	pbuf += sprintf(pbuf, "\nRcvr status1: %s (%02Xh); ",
			text, status1);

	pbuf += sprintf(pbuf, "status2: %s, %s (%02Xh)",
			(status2 & 0x01)?"No BBRAM":"BBRAM OK",
			(status2 & 0x10)?"No Ant":"Ant OK",
			status2);
}

/* 0x47 */
static void
rpt_SNR_all_SVs(
		TSIPPKT *rpt
		)
{
	unsigned char
	    nsvs, sv_prn[12];
	short
	    isv;
	float
	    snr[12];

	/* unload rptbuf */
	if (rpt_0x47 (rpt, &nsvs, sv_prn, snr))
	{
		parsed = BADLEN_PARSE;
		return;
	}

	pbuf += sprintf(pbuf, "\nSNR for satellites: %d", nsvs);
	for (isv = 0; isv < nsvs; isv++)
	{
		pbuf += sprintf(pbuf, "\n    SV %02d   %6.2f",
				sv_prn[isv], snr[isv]);
	}
}

/* 0x48 */
static void
rpt_GPS_system_message(
		       TSIPPKT *rpt
		       )
{
	unsigned char
	    message[23];

	/* unload rptbuf */
	if (rpt_0x48 (rpt, message))
	{
		parsed = BADLEN_PARSE;
		return;
	}

	pbuf += sprintf(pbuf, "\nGPS message: %s", message);
}

/* 0x49 */
static void
rpt_almanac_health_page(
			TSIPPKT *rpt
			)
{
	short
	    iprn;
	unsigned char
	    sv_health [32];

	/* unload rptbuf */
	if (rpt_0x49 (rpt, sv_health))
	{
		parsed = BADLEN_PARSE;
		return;
	}

	pbuf += sprintf(pbuf, "\nAlmanac health page:");
	for (iprn = 0; iprn < 32; iprn++)
	{
		if (!(iprn%5)) *pbuf++ = '\n';
		pbuf += sprintf(pbuf, "    SV%02d  %2X",
				(iprn+1) , sv_health[iprn]);
	}
}

/* 0x4A */
static void
rpt_single_lla_position(
			TSIPPKT *rpt
			)
{
	short
	    lat_deg, lon_deg;
	float
	    lat, lon,
	    alt, clock_bias, time_of_fix;
	double lat_min, lon_min;
	unsigned char
	    north_south, east_west;

	if (rpt_0x4A (rpt,
		      &lat, &lon, &alt, &clock_bias, &time_of_fix))
	{
		parsed = BADLEN_PARSE;
		return;
	}

	/* convert from radians to degrees */
	lat *= (float)R2D;
	north_south = 'N';
	if (lat < 0.0)
	{
		north_south = 'S';
		lat = -lat;
	}
	lat_deg = (short)lat;
	lat_min = (lat - lat_deg) * 60.0;

	lon *= (float)R2D;
	east_west = 'E';
	if (lon < 0.0)
	{
		east_west = 'W';
		lon = -lon;
	}
	lon_deg = (short)lon;
	lon_min = (lon - lon_deg) * 60.0;

	pbuf += sprintf(pbuf, "\nSLLA: %4d: %06.3f  %c%5d:%06.3f  %c%10.2f  %12.2f%s",
			lat_deg, lat_min, north_south,
			lon_deg, lon_min, east_west,
			alt, clock_bias,
			show_time(time_of_fix));
}

/* 0x4A */
static void
rpt_ref_alt(
	    TSIPPKT *rpt
	    )
{
	float
	    alt, dummy;
	unsigned char
	    alt_flag;

	if (rpt_0x4A_2 (rpt, &alt, &dummy, &alt_flag))
	{
		parsed = BADLEN_PARSE;
		return;
	}

	pbuf += sprintf(pbuf, "\nReference Alt:   %.1f m;    %s",
			alt, alt_flag?"ON":"OFF");
}

/* 0x4B */
static void
rpt_rcvr_id_and_status(
		       TSIPPKT *rpt
		       )
{

	unsigned char
	    machine_id, status3, status4;

	/* unload rptbuf */
	if (rpt_0x4B (rpt, &machine_id, &status3, &status4))
	{
		parsed = BADLEN_PARSE;
		return;
	}

	pbuf += sprintf(pbuf, "\nRcvr Machine ID: %d; Status3 = %s, %s (%02Xh)",
			machine_id,
			(status3 & 0x02)?"No RTC":"RTC OK",
			(status3 & 0x08)?"No Alm":"Alm OK",
			status3);
}

/* 0x4C */
static void
rpt_operating_parameters(
			 TSIPPKT *rpt
			 )
{
	unsigned char
	    dyn_code;
	float
	    el_mask, snr_mask, dop_mask, dop_switch;

	/* unload rptbuf */
	if (rpt_0x4C (rpt, &dyn_code, &el_mask,
		      &snr_mask, &dop_mask, &dop_switch))
	{
		parsed = BADLEN_PARSE;
		return;
	}

	pbuf += sprintf(pbuf, "\nOperating Parameters:");
	pbuf += sprintf(pbuf, "\n     Dynamics code = %d %s",
			dyn_code, dyn_text[dyn_code]);
	pbuf += sprintf(pbuf, "\n     Elevation mask = %.2f", el_mask * R2D);
	pbuf += sprintf(pbuf, "\n     SNR mask = %.2f", snr_mask);
	pbuf += sprintf(pbuf, "\n     DOP mask = %.2f", dop_mask);
	pbuf += sprintf(pbuf, "\n     DOP switch = %.2f", dop_switch);
}

/* 0x4D */
static void
rpt_oscillator_offset(
		      TSIPPKT *rpt
		      )
{
	float
	    osc_offset;

	/* unload rptbuf */
	if (rpt_0x4D (rpt, &osc_offset))
	{
		parsed = BADLEN_PARSE;
		return;
	}

	pbuf += sprintf(pbuf, "\nOscillator offset: %.2f Hz = %.3f PPM",
			osc_offset, osc_offset/1575.42);
}

/* 0x4E */
static void
rpt_GPS_time_set_response(
			  TSIPPKT *rpt
			  )
{
	unsigned char
	    response;

	/* unload rptbuf */
	if (rpt_0x4E (rpt, &response))
	{
		parsed = BADLEN_PARSE;
		return;
	}

	switch (response)
	{
	    case 'Y':
		pbuf += sprintf(pbuf, "\nTime set accepted");
		break;

	    case 'N':
		pbuf += sprintf(pbuf, "\nTime set rejected or not required");
		break;

	    default:
		parsed = BADDATA_PARSE;
	}
}

/* 0x4F */
static void
rpt_UTC_offset(
	       TSIPPKT *rpt
	       )
{
	double
	    a0;
	float
	    a1, time_of_data;
	short
	    dt_ls, wn_t, wn_lsf, dn, dt_lsf;

	/* unload rptbuf */
	if (rpt_0x4F (rpt, &a0, &a1, &time_of_data,
		      &dt_ls, &wn_t, &wn_lsf, &dn, &dt_lsf)) {
		parsed = BADLEN_PARSE;
		return;
	}

	pbuf += sprintf(pbuf, "\nUTC Correction Data");
	pbuf += sprintf(pbuf, "\n   A_0         = %g  ", a0);
	pbuf += sprintf(pbuf, "\n   A_1         = %g  ", a1);
	pbuf += sprintf(pbuf, "\n   delta_t_LS  = %d  ", dt_ls);
	pbuf += sprintf(pbuf, "\n   t_ot        = %.0f  ", time_of_data);
	pbuf += sprintf(pbuf, "\n   WN_t        = %d  ", wn_t );
	pbuf += sprintf(pbuf, "\n   WN_LSF      = %d  ", wn_lsf );
	pbuf += sprintf(pbuf, "\n   DN          = %d  ", dn );
	pbuf += sprintf(pbuf, "\n   delta_t_LSF = %d  ", dt_lsf );
}

/**/
/* 0x54 */
static void
rpt_1SV_bias(
	     TSIPPKT *rpt
	     )
{
	float
	    clock_bias, freq_offset, time_of_fix;

	/* unload rptbuf */
	if (rpt_0x54 (rpt, &clock_bias, &freq_offset, &time_of_fix)) {
		parsed = BADLEN_PARSE;
		return;
	}

	pbuf += sprintf (pbuf, "\nTime Fix   Clock Bias: %6.2f m  Freq Bias: %6.2f m/s%s",
			 clock_bias, freq_offset, show_time (time_of_fix));
}

/* 0x55 */
static void
rpt_io_opt(
	   TSIPPKT *rpt
	   )
{
	unsigned char
	    pos_code, vel_code, time_code, aux_code;

	/* unload rptbuf */
	if (rpt_0x55 (rpt,
		      &pos_code, &vel_code, &time_code, &aux_code)) {
		parsed = BADLEN_PARSE;
		return;
	}
	/* rptbuf unloaded */

	pbuf += sprintf(pbuf, "\nI/O Options: %2X %2X %2X %2X",
			pos_code, vel_code, time_code, aux_code);

	if (pos_code & 0x01) {
		pbuf += sprintf(pbuf, "\n    ECEF XYZ position output");
	}

	if (pos_code & 0x02) {
		pbuf += sprintf(pbuf, "\n    LLA position output");
	}

	pbuf += sprintf(pbuf, (pos_code & 0x04)?
			"\n    MSL altitude output (Geoid height) ":
			"\n    WGS-84 altitude output");

	pbuf += sprintf(pbuf, (pos_code & 0x08)?
			"\n    MSL altitude input":
			"\n    WGS-84 altitude input");

	pbuf += sprintf(pbuf, (pos_code & 0x10)?
			"\n    Double precision":
			"\n    Single precision");

	if (pos_code & 0x20) {
		pbuf += sprintf(pbuf, "\n    All Enabled Superpackets");
	}

	if (vel_code & 0x01) {
		pbuf += sprintf(pbuf, "\n    ECEF XYZ velocity output");
	}

	if (vel_code & 0x02) {
		pbuf += sprintf(pbuf, "\n    ENU velocity output");
	}

	pbuf += sprintf(pbuf, (time_code & 0x01)?
			"\n    Time tags in UTC":
			"\n    Time tags in GPS time");

	if (time_code & 0x02) {
		pbuf += sprintf(pbuf, "\n    Fixes delayed to integer seconds");
	}

	if (time_code & 0x04) {
		pbuf += sprintf(pbuf, "\n    Fixes sent only on request");
	}

	if (time_code & 0x08) {
		pbuf += sprintf(pbuf, "\n    Synchronized measurements");
	}

	if (time_code & 0x10) {
		pbuf += sprintf(pbuf, "\n    Minimize measurement propagation");
	}

	pbuf += sprintf(pbuf, (time_code & 0x20) ?
			"\n    PPS output at all times" :
			"\n    PPS output during fixes");

	if (aux_code & 0x01) {
		pbuf += sprintf(pbuf, "\n    Raw measurement output");
	}

	if (aux_code & 0x02) {
		pbuf += sprintf(pbuf, "\n    Code-phase smoothed before output");
	}

	if (aux_code & 0x04) {
		pbuf += sprintf(pbuf, "\n    Additional fix status");
	}

	pbuf += sprintf(pbuf, (aux_code & 0x08)?
			"\n    Signal Strength Output as dBHz" :
			"\n    Signal Strength Output as AMU");
}

/* 0x56 */
static void
rpt_ENU_velocity(
		 TSIPPKT *rpt
		 )
{
	float
	    vel_ENU[3], freq_offset, time_of_fix;

	/* unload rptbuf */
	if (rpt_0x56 (rpt, vel_ENU, &freq_offset, &time_of_fix)) {
		parsed = BADLEN_PARSE;
		return;
	}

	pbuf += sprintf(pbuf, "\nVel ENU: %11.3f  %11.3f  %11.3f  %12.3f%s",
			vel_ENU[0], vel_ENU[1], vel_ENU[2], freq_offset,
			show_time (time_of_fix));
}

/* 0x57 */
static void
rpt_last_fix_info(
		  TSIPPKT *rpt
		  )
{
	unsigned char
	    source_code, diag_code;
	short
	    week_num;
	float
	    time_of_fix;

	/* unload rptbuf */
	if (rpt_0x57 (rpt, &source_code, &diag_code, &week_num, &time_of_fix)) {
		parsed = BADLEN_PARSE;
		return;
	}

	pbuf += sprintf(pbuf, "\n source code %d;   diag code: %2Xh",
			source_code, diag_code);
	pbuf += sprintf(pbuf, "\n    Time of last fix:%s", show_time(time_of_fix));
	pbuf += sprintf(pbuf, "\n    Week of last fix: %d", week_num);
}

/* 0x58 */
static void
rpt_GPS_system_data(
		    TSIPPKT *rpt
		    )
{
	unsigned char
	    iprn,
	    op_code, data_type, sv_prn,
	    data_length, data_packet[250];
	ALM_INFO
	    *almanac;
	ALH_PARMS
	    *almh;
	UTC_INFO
	    *utc;
	ION_INFO
	    *ionosphere;
	EPHEM_CLOCK
	    *cdata;
	EPHEM_ORBIT
	    *edata;
	NAV_INFO
	    *nav_data;
	unsigned char
	    curr_t_oa;
	unsigned short
	    curr_wn_oa;
	static char
	    *datname[] =
	    {"", "", "Almanac Orbit",
	     "Health Page & Ref Time", "Ionosphere", "UTC ",
	     "Ephemeris"};

	/* unload rptbuf */
	if (rpt_0x58 (rpt, &op_code, &data_type, &sv_prn,
		      &data_length, data_packet))
	{
		parsed = BADLEN_PARSE;
		return;
	}

	pbuf += sprintf(pbuf, "\nSystem data [%d]:  %s  SV%02d",
			data_type, datname[data_type], sv_prn);
	switch (op_code)
	{
	    case 1:
		pbuf += sprintf(pbuf, "  Acknowledgment");
		break;
	    case 2:
		pbuf += sprintf(pbuf, "  length = %d bytes", data_length);
		switch (data_type) {
		    case 2:
			/* Almanac */
			if (sv_prn == 0 || sv_prn > 32) {
				pbuf += sprintf(pbuf, "  Binary PRN invalid");
				return;
			}
			almanac = (ALM_INFO*)data_packet;
			pbuf += sprintf(pbuf, "\n   t_oa_raw = % -12d    SV_hlth  = % -12d  ",
					almanac->t_oa_raw , almanac->SV_health );
			pbuf += sprintf(pbuf, "\n   e        = % -12g    t_oa     = % -12g  ",
					almanac->e        , almanac->t_oa     );
			pbuf += sprintf(pbuf, "\n   i_0      = % -12g    OMEGADOT = % -12g  ",
					almanac->i_0      , almanac->OMEGADOT );
			pbuf += sprintf(pbuf, "\n   sqrt_A   = % -12g    OMEGA_0  = % -12g  ",
					almanac->sqrt_A   , almanac->OMEGA_0  );
			pbuf += sprintf(pbuf, "\n   omega    = % -12g    M_0      = % -12g  ",
					almanac->omega    , almanac->M_0      );
			pbuf += sprintf(pbuf, "\n   a_f0     = % -12g    a_f1     = % -12g  ",
					almanac->a_f0     , almanac->a_f1     );
			pbuf += sprintf(pbuf, "\n   Axis     = % -12g    n        = % -12g  ",
					almanac->Axis     , almanac->n        );
			pbuf += sprintf(pbuf, "\n   OMEGA_n  = % -12g    ODOT_n   = % -12g  ",
					almanac->OMEGA_n  , almanac->ODOT_n   );
			pbuf += sprintf(pbuf, "\n   t_zc     = % -12g    weeknum  = % -12d  ",
					almanac->t_zc     , almanac->weeknum  );
			pbuf += sprintf(pbuf, "\n   wn_oa    = % -12d", almanac->wn_oa    );
			break;

		    case 3:
			/* Almanac health page */
			almh = (ALH_PARMS*)data_packet;
			pbuf += sprintf(pbuf, "\n   t_oa = %d, wn_oa&0xFF = %d  ",
					almh->t_oa, almh->WN_a);
			pbuf += sprintf(pbuf, "\nAlmanac health page:");
			for (iprn = 0; iprn < 32; iprn++) {
				if (!(iprn%5)) *pbuf++ = '\n';
				pbuf += sprintf(pbuf, "    SV%02d  %2X",
						(iprn+1) , almh->SV_health[iprn]);
			}
			curr_t_oa = data_packet[34];
			curr_wn_oa = (unsigned short)((data_packet[35]<<8) + data_packet[36]);
			pbuf += sprintf(pbuf, "\n   current t_oa = %d, wn_oa = %d  ",
					curr_t_oa, curr_wn_oa);
			break;

		    case 4:
			/* Ionosphere */
			ionosphere = (ION_INFO*)data_packet;
			pbuf += sprintf(pbuf, "\n   alpha_0 = % -12g  alpha_1 = % -12g ",
					ionosphere->alpha_0, ionosphere->alpha_1);
			pbuf += sprintf(pbuf, "\n   alpha_2 = % -12g  alpha_3 = % -12g ",
					ionosphere->alpha_2, ionosphere->alpha_3);
			pbuf += sprintf(pbuf, "\n   beta_0  = % -12g  beta_1  = % -12g  ",
					ionosphere->beta_0, ionosphere->beta_1);
			pbuf += sprintf(pbuf, "\n   beta_2  = % -12g  beta_3  = % -12g  ",
					ionosphere->beta_2, ionosphere->beta_3);
			break;

		    case 5:
			/* UTC */
			utc = (UTC_INFO*)data_packet;
			pbuf += sprintf(pbuf, "\n   A_0         = %g  ", utc->A_0);
			pbuf += sprintf(pbuf, "\n   A_1         = %g  ", utc->A_1);
			pbuf += sprintf(pbuf, "\n   delta_t_LS  = %d  ", utc->delta_t_LS);
			pbuf += sprintf(pbuf, "\n   t_ot        = %.0f  ", utc->t_ot );
			pbuf += sprintf(pbuf, "\n   WN_t        = %d  ", utc->WN_t );
			pbuf += sprintf(pbuf, "\n   WN_LSF      = %d  ", utc->WN_LSF );
			pbuf += sprintf(pbuf, "\n   DN          = %d  ", utc->DN );
			pbuf += sprintf(pbuf, "\n   delta_t_LSF = %d  ", utc->delta_t_LSF );
			break;

		    case 6: /* Ephemeris */
			if (sv_prn == 0 || sv_prn > 32) {
				pbuf += sprintf(pbuf, "  Binary PRN invalid");
				return;
			}
			nav_data = (NAV_INFO*)data_packet;

			pbuf += sprintf(pbuf, "\n     SV_PRN = % -12d .  t_ephem = % -12g . ",
					nav_data->sv_number , nav_data->t_ephem );
			cdata = &(nav_data->ephclk);
			pbuf += sprintf(pbuf,
					"\n    weeknum = % -12d .   codeL2 = % -12d .  L2Pdata = % -12d",
					cdata->weeknum , cdata->codeL2 , cdata->L2Pdata );
			pbuf += sprintf(pbuf,
					"\n  SVacc_raw = % -12d .SV_health = % -12d .     IODC = % -12d",
					cdata->SVacc_raw, cdata->SV_health, cdata->IODC );
			pbuf += sprintf(pbuf,
					"\n       T_GD = % -12g .     t_oc = % -12g .     a_f2 = % -12g",
					cdata->T_GD, cdata->t_oc, cdata->a_f2 );
			pbuf += sprintf(pbuf,
					"\n       a_f1 = % -12g .     a_f0 = % -12g .    SVacc = % -12g",
					cdata->a_f1, cdata->a_f0, cdata->SVacc );
			edata = &(nav_data->ephorb);
			pbuf += sprintf(pbuf,
					"\n       IODE = % -12d .fit_intvl = % -12d .     C_rs = % -12g",
					edata->IODE, edata->fit_interval, edata->C_rs );
			pbuf += sprintf(pbuf,
					"\n    delta_n = % -12g .      M_0 = % -12g .     C_uc = % -12g",
					edata->delta_n, edata->M_0, edata->C_uc );
			pbuf += sprintf(pbuf,
					"\n        ecc = % -12g .     C_us = % -12g .   sqrt_A = % -12g",
					edata->e, edata->C_us, edata->sqrt_A );
			pbuf += sprintf(pbuf,
					"\n       t_oe = % -12g .     C_ic = % -12g .  OMEGA_0 = % -12g",
					edata->t_oe, edata->C_ic, edata->OMEGA_0 );
			pbuf += sprintf(pbuf,
					"\n       C_is = % -12g .      i_0 = % -12g .     C_rc = % -12g",
					edata->C_is, edata->i_0, edata->C_rc );
			pbuf += sprintf(pbuf,
					"\n      omega = % -12g . OMEGADOT = % -12g .     IDOT = % -12g",
					edata->omega, edata->OMEGADOT, edata->IDOT );
			pbuf += sprintf(pbuf,
					"\n       Axis = % -12g .        n = % -12g .    r1me2 = % -12g",
					edata->Axis, edata->n, edata->r1me2 );
			pbuf += sprintf(pbuf,
					"\n    OMEGA_n = % -12g .   ODOT_n = % -12g",
					edata->OMEGA_n, edata->ODOT_n );
			break;
		}
	}
}


/* 0x59: */
static void
rpt_SVs_enabled(
		TSIPPKT *rpt
		)
{
	unsigned char
	    numsvs,
	    code_type,
	    status_code[32];
	short
	    iprn;

	/* unload rptbuf */
	if (rpt_0x59 (rpt, &code_type, status_code))
	{
		parsed = BADLEN_PARSE;
		return;
	}
	switch (code_type)
	{
	    case 3: pbuf += sprintf(pbuf, "\nSVs Disabled:\n"); break;
	    case 6: pbuf += sprintf(pbuf, "\nSVs with Health Ignored:\n"); break;
	    default: return;
	}
	numsvs = 0;
	for (iprn = 0; iprn < 32; iprn++)
	{
		if (status_code[iprn])
		{
			pbuf += sprintf(pbuf, " %02d", iprn+1);
			numsvs++;
		}
	}
	if (numsvs == 0) pbuf += sprintf(pbuf, "None");
}


/* 0x5A */
static void
rpt_raw_msmt(
	     TSIPPKT *rpt
	     )
{
	unsigned char
	    sv_prn;
	float
	    sample_length, signal_level, code_phase, Doppler;
	double
	    time_of_fix;

	/* unload rptbuf */
	if (rpt_0x5A (rpt, &sv_prn, &sample_length, &signal_level,
		      &code_phase, &Doppler, &time_of_fix))
	{
		parsed = BADLEN_PARSE;
		return;
	}

	pbuf += sprintf(pbuf, "\n   %02d %5.0f %7.1f %10.2f %10.2f %12.3f %s",
			sv_prn, sample_length, signal_level, code_phase, Doppler, time_of_fix,
			show_time ((float)time_of_fix));
}

/* 0x5B */
static void
rpt_SV_ephemeris_status(
			TSIPPKT *rpt
			)
{
	unsigned char
	    sv_prn, sv_health, sv_iode, fit_interval_flag;
	float
	    time_of_collection, time_of_eph, sv_accy;

	/* unload rptbuf */
	if (rpt_0x5B (rpt, &sv_prn, &sv_health, &sv_iode, &fit_interval_flag,
		      &time_of_collection, &time_of_eph, &sv_accy))
	{
		parsed = BADLEN_PARSE;
		return;
	}

	pbuf += sprintf(pbuf, "\n  SV%02d  %s   %2Xh     %2Xh ",
			sv_prn, show_time (time_of_collection), sv_health, sv_iode);
	/* note: cannot use show_time twice in same call */
	pbuf += sprintf(pbuf, "%s   %1d   %4.1f",
			show_time (time_of_eph), fit_interval_flag, sv_accy);
}

/* 0x5C */
static void
rpt_SV_tracking_status(
		       TSIPPKT *rpt
		       )
{
	unsigned char
	    sv_prn, chan, slot, acq_flag, eph_flag,
	    old_msmt_flag, integer_msec_flag, bad_data_flag,
	    data_collect_flag;
	float
	    signal_level, time_of_last_msmt,
	    elev, azim;

	/* unload rptbuf */
	if (rpt_0x5C (rpt,
		      &sv_prn, &slot, &chan, &acq_flag, &eph_flag,
		      &signal_level, &time_of_last_msmt, &elev, &azim,
		      &old_msmt_flag, &integer_msec_flag, &bad_data_flag,
		      &data_collect_flag))
	{
		parsed = BADLEN_PARSE;
		return;
	}

	pbuf += sprintf(pbuf,
			"\n SV%2d  %1d   %1d   %1d   %4.1f  %s  %5.1f  %5.1f",
			sv_prn, chan,
			acq_flag, eph_flag, signal_level,
			show_time(time_of_last_msmt),
			elev*R2D, azim*R2D);
}

/**/
/* 0x6D */
static void
rpt_allSV_selection(
		    TSIPPKT *rpt
		    )
{
	unsigned char
	    manual_mode, nsvs, sv_prn[8], ndim;
	short
	    islot;
	float
	    pdop, hdop, vdop, tdop;

	/* unload rptbuf */
	if (rpt_0x6D (rpt,
		      &manual_mode, &nsvs, &ndim, sv_prn,
		      &pdop, &hdop, &vdop, &tdop))
	{
		parsed = BADLEN_PARSE;
		return;
	}

	switch (ndim)
	{
	    case 0:
		pbuf += sprintf(pbuf, "\nMode: Searching, %d-SV:", nsvs);
		break;
	    case 1:
		pbuf += sprintf(pbuf, "\nMode: One-SV Timing:");
		break;
	    case 3: case 4:
		pbuf += sprintf(pbuf, "\nMode: %c-%dD, %d-SV:",
				manual_mode ? 'M' : 'A', ndim - 1,  nsvs);
		break;
	    case 5:
		pbuf += sprintf(pbuf, "\nMode: Timing, %d-SV:", nsvs);
		break;
	    default:
		pbuf += sprintf(pbuf, "\nMode: Unknown = %d:", ndim);
		break;
	}

	for (islot = 0; islot < nsvs; islot++)
	{
		if (sv_prn[islot]) pbuf += sprintf(pbuf, " %02d", sv_prn[islot]);
	}
	if (ndim == 3 || ndim == 4)
	{
		pbuf += sprintf(pbuf, ";  DOPs: P %.1f H %.1f V %.1f T %.1f",
				pdop, hdop, vdop, tdop);
	}
}

/**/
/* 0x82 */
static void
rpt_DGPS_position_mode(
		       TSIPPKT *rpt
		       )
{
	unsigned char
	    diff_mode;

	/* unload rptbuf */
	if (rpt_0x82 (rpt, &diff_mode)) {
		parsed = BADLEN_PARSE;
		return;
	}

	pbuf += sprintf(pbuf, "\nFix is%s DGPS-corrected (%s mode)  (%d)",
			(diff_mode&1) ? "" : " not",
			(diff_mode&2) ? "auto" : "manual",
			diff_mode);
}

/* 0x83 */
static void
rpt_double_ECEF_position(
			 TSIPPKT *rpt
			 )
{
	double
	    ECEF_pos[3], clock_bias;
	float
	    time_of_fix;

	/* unload rptbuf */
	if (rpt_0x83 (rpt, ECEF_pos, &clock_bias, &time_of_fix))
	{
		parsed = BADLEN_PARSE;
		return;
	}

	pbuf += sprintf(pbuf, "\nDXYZ:%12.2f  %13.2f  %13.2f %12.2f%s",
			ECEF_pos[0], ECEF_pos[1], ECEF_pos[2], clock_bias,
			show_time(time_of_fix));
}

/* 0x84 */
static void
rpt_double_lla_position(
			TSIPPKT *rpt
			)
{
	short
	    lat_deg, lon_deg;
	double
	    lat, lon, lat_min, lon_min,
	    alt, clock_bias;
	float
	    time_of_fix;
	unsigned char
	    north_south, east_west;

	/* unload rptbuf */
	if (rpt_0x84 (rpt,
		      &lat, &lon, &alt, &clock_bias, &time_of_fix))
	{
		parsed = BADLEN_PARSE;
		return;
	}

	lat *= R2D;
	lon *= R2D;
	if (lat < 0.0) {
		north_south = 'S';
		lat = -lat;
	} else {
		north_south = 'N';
	}
	lat_deg = (short)lat;
	lat_min = (lat - lat_deg) * 60.0;

	if (lon < 0.0) {
		east_west = 'W';
		lon = -lon;
	} else {
		east_west = 'E';
	}
	lon_deg = (short)lon;
	lon_min = (lon - lon_deg) * 60.0;
	pbuf += sprintf(pbuf, "\nDLLA: %2d:%08.5f %c; %3d:%08.5f %c; %10.2f %12.2f%s",
			lat_deg, lat_min, north_south,
			lon_deg, lon_min, east_west,
			alt, clock_bias,
			show_time(time_of_fix));
}

/* 0xBB */
static void
rpt_complete_rcvr_config(
			 TSIPPKT *rpt
			 )
{
	TSIP_RCVR_CFG TsipxBB ;
	/* unload rptbuf */
	if (rpt_Paly0xBB (rpt, &TsipxBB))
	{
		parsed = BADLEN_PARSE;
		return;
	}

	pbuf += sprintf(pbuf, "\n   operating mode:      %s",
			NavModeText0xBB[TsipxBB.operating_mode]);
	pbuf += sprintf(pbuf, "\n   dynamics:            %s",
			dyn_text[TsipxBB.dyn_code]);
	pbuf += sprintf(pbuf, "\n   elev angle mask:     %g deg",
			TsipxBB.elev_mask * R2D);
	pbuf += sprintf(pbuf, "\n   SNR mask:            %g AMU",
			TsipxBB.cno_mask);
	pbuf += sprintf(pbuf, "\n   DOP mask:            %g",
			TsipxBB.dop_mask);
	pbuf += sprintf(pbuf, "\n   DOP switch:          %g",
			TsipxBB.dop_switch);
	return ;
}

/* 0xBC */
static void
rpt_rcvr_serial_port_config(
			    TSIPPKT *rpt
			    )
{
	unsigned char
	    port_num, in_baud, out_baud, data_bits, parity, stop_bits, flow_control,
	    protocols_in, protocols_out, reserved;
	unsigned char known;

	/* unload rptbuf */
	if (rpt_0xBC (rpt, &port_num, &in_baud, &out_baud, &data_bits, &parity,
		      &stop_bits, &flow_control, &protocols_in, &protocols_out, &reserved)) {
		parsed = BADLEN_PARSE;
		return;
	}
	/* rptbuf unloaded */

	pbuf += sprintf(pbuf, "\n   RECEIVER serial port %s config:",
			rcvr_port_text[port_num]);

	pbuf += sprintf(pbuf, "\n             I/O Baud %s/%s, %d - %s - %d",
			st_baud_text_app[in_baud],
			st_baud_text_app[out_baud],
			data_bits+5,
			parity_text[parity],
			stop_bits=1);
	pbuf += sprintf(pbuf, "\n             Input protocols: ");
	known = FALSE;
	if (protocols_in&B_TSIP)
	{
		pbuf += sprintf(pbuf, "%s ", protocols_in_text[1]);
		known = TRUE;
	}
	if (known == FALSE) pbuf += sprintf(pbuf, "No known");

	pbuf += sprintf(pbuf, "\n             Output protocols: ");
	known = FALSE;
	if (protocols_out&B_TSIP)
	{
		pbuf += sprintf(pbuf, "%s ", protocols_out_text[1]);
		known = TRUE;
	}
	if (protocols_out&B_NMEA)
	{
		pbuf += sprintf(pbuf, "%s ", protocols_out_text[2]);
		known = TRUE;
	}
	if (known == FALSE) pbuf += sprintf(pbuf, "No known");
	reserved = reserved;

}

/* 0x8F */
/* 8F0B */
static void
rpt_8F0B(
	 TSIPPKT *rpt
	 )
{
	const char
	    *oprtng_dim[7] = {
		"horizontal (2-D)",
		"full position (3-D)",
		"single satellite (0-D)",
		"automatic",
		"N/A",
		"N/A",
		"overdetermined clock"};
	char
	    sv_id[8];
	unsigned char
	    month,
	    date,
	    dim_mode,
	    north_south,
	    east_west;
	unsigned short
	    event;
	short
	    utc_offset,
	    year,
	    local_index;
	short
	    lat_deg,
	    lon_deg;
	float
	    bias_unc,
	    dr_unc;
	double
	    tow,
	    bias,
	    drift,
	    lat,
	    lon,
	    alt,
	    lat_min,
	    lon_min;
	int
	    numfix,
	    numnotfix;

	if (rpt_0x8F0B(rpt,
		       &event,
		       &tow,
		       &date,
		       &month,
		       &year,
		       &dim_mode,
		       &utc_offset,
		       &bias,
		       &drift,
		       &bias_unc,
		       &dr_unc,
		       &lat,
		       &lon,
		       &alt,
		       sv_id))
	{
		parsed = BADLEN_PARSE;
		return;
	}

	if (event == 0)
	{
		pbuf += sprintf(pbuf, "\nNew partial+full meas");
	}
	else
	{
		pbuf += sprintf(pbuf, "\nEvent count: %5d", event);
	}

	pbuf += sprintf(pbuf, "\nGPS time  : %s %2d/%2d/%2d (DMY)",
			show_time(tow), date, month, year);
	pbuf += sprintf(pbuf, "\nMode      : %s", oprtng_dim[dim_mode]);
	pbuf += sprintf(pbuf, "\nUTC offset: %2d", utc_offset);
	pbuf += sprintf(pbuf, "\nClock Bias: %6.2f m", bias);
	pbuf += sprintf(pbuf, "\nFreq bias : %6.2f m/s", drift);
	pbuf += sprintf(pbuf, "\nBias unc  : %6.2f m", bias_unc);
	pbuf += sprintf(pbuf, "\nFreq unc  : %6.2f m/s", dr_unc);

	lat *= R2D; /* convert from radians to degrees */
	lon *= R2D;
	if (lat < 0.0)
	{
		north_south = 'S';
		lat = -lat;
	}
	else
	{
		north_south = 'N';
	}

	lat_deg = (short)lat;
	lat_min = (lat - lat_deg) * 60.0;
	if (lon < 0.0)
	{
		east_west = 'W';
		lon = -lon;
	}
	else
	{
		east_west = 'E';
	}

	lon_deg = (short)lon;
	lon_min = (lon - lon_deg) * 60.0;
	pbuf += sprintf(pbuf, "\nPosition  :");
	pbuf += sprintf(pbuf, " %4d %6.3f %c", lat_deg, lat_min, north_south);
	pbuf += sprintf(pbuf, " %5d %6.3f %c", lon_deg, lon_min, east_west);
	pbuf += sprintf(pbuf, " %10.2f", alt);

	numfix = numnotfix = 0;
	for (local_index=0; local_index<8; local_index++)
	{
		if (sv_id[local_index] < 0) numnotfix++;
		if (sv_id[local_index] > 0) numfix++;
	}
	if (numfix > 0)
	{
		pbuf += sprintf(pbuf, "\nSVs used in fix  : ");
		for (local_index=0; local_index<8; local_index++)
		{
			if (sv_id[local_index] > 0)
			{
				pbuf += sprintf(pbuf, "%2d ", sv_id[local_index]);
			}
		}
	}
	if (numnotfix > 0)
	{
		pbuf += sprintf(pbuf, "\nOther SVs tracked: ");
		for (local_index=0; local_index<8; local_index++)
		{
			if (sv_id[local_index] < 0)
			{
				pbuf += sprintf(pbuf, "%2d ", sv_id[local_index]);
			}
		}
	}
}

/* 0x8F14 */
/* Datum parameters */
static void
rpt_8F14(
	 TSIPPKT *rpt
	 )
{
	double
	    datum_coeffs[5];
	short
	    datum_idx;

	/* unload rptbuf */
	if (rpt_0x8F14 (rpt, &datum_idx, datum_coeffs))
	{
		parsed = BADLEN_PARSE;
		return;
	}

	if (datum_idx == -1)
	{
		pbuf += sprintf(pbuf, "\nUser-Entered Datum:");
		pbuf += sprintf(pbuf, "\n   dx        = %6.1f", datum_coeffs[0]);
		pbuf += sprintf(pbuf, "\n   dy        = %6.1f", datum_coeffs[1]);
		pbuf += sprintf(pbuf, "\n   dz        = %6.1f", datum_coeffs[2]);
		pbuf += sprintf(pbuf, "\n   a-axis    = %10.3f", datum_coeffs[3]);
		pbuf += sprintf(pbuf, "\n   e-squared = %16.14f", datum_coeffs[4]);
	}
	else if (datum_idx == 0)
	{
		pbuf += sprintf(pbuf, "\nWGS-84 datum, Index 0 ");
	}
	else
	{
		pbuf += sprintf(pbuf, "\nStandard Datum, Index %3d ", datum_idx);
	}
}

/* 0x8F15 */
/* Datum parameters */
static void
rpt_8F15(
	 TSIPPKT *rpt
	 )
{
	double
	    datum_coeffs[5];
	short
	    datum_idx;

	/* unload rptbuf */
	if (rpt_0x8F15 (rpt, &datum_idx, datum_coeffs)) {
		parsed = BADLEN_PARSE;
		return;
	}

	if (datum_idx == -1)
	{
		pbuf += sprintf(pbuf, "\nUser-Entered Datum:");
		pbuf += sprintf(pbuf, "\n   dx        = %6.1f", datum_coeffs[0]);
		pbuf += sprintf(pbuf, "\n   dy        = %6.1f", datum_coeffs[1]);
		pbuf += sprintf(pbuf, "\n   dz        = %6.1f", datum_coeffs[2]);
		pbuf += sprintf(pbuf, "\n   a-axis    = %10.3f", datum_coeffs[3]);
		pbuf += sprintf(pbuf, "\n   e-squared = %16.14f", datum_coeffs[4]);
	}
	else if (datum_idx == 0)
	{
		pbuf += sprintf(pbuf, "\nWGS-84 datum, Index 0 ");
	}
	else
	{
		pbuf += sprintf(pbuf, "\nStandard Datum, Index %3d ", datum_idx);
	}
}

/* 0x8F20 */
#define INFO_DGPS       0x02
#define INFO_2D         0x04
#define INFO_ALTSET     0x08
#define INFO_FILTERED   0x10
static void
rpt_8F20(
	 TSIPPKT *rpt
	 )
{
	unsigned char
	    info, nsvs, sv_prn[32];
	short
	    week_num, datum_index, sv_IODC[32];
	double
	    lat, lon, alt, time_of_fix;
	double
	    londeg, latdeg, vel[3];
	short
	    isv;
	char
	    datum_string[20];

	/* unload rptbuf */
	if (rpt_0x8F20 (rpt,
			&info, &lat, &lon, &alt, vel,
			&time_of_fix,
			&week_num, &nsvs, sv_prn, sv_IODC, &datum_index))
	{
		parsed = BADLEN_PARSE;
		return;
	}
	pbuf += sprintf(pbuf,
			"\nFix at: %04d:%3s:%02d:%02d:%06.3f GPS (=UTC+%2ds)  FixType: %s%s%s",
			week_num,
			dayname[(short)(time_of_fix/86400.0)],
			(short)fmod(time_of_fix/3600., 24.),
			(short)fmod(time_of_fix/60., 60.),
			fmod(time_of_fix, 60.),
			(char)rpt->buf[29],		/* UTC offset */
			(info & INFO_DGPS)?"Diff":"",
			(info & INFO_2D)?"2D":"3D",
			(info & INFO_FILTERED)?"-Filtrd":"");

	if (datum_index > 0)
	{
		sprintf(datum_string, "Datum%3d", datum_index);
	}
	else if (datum_index)
	{
		sprintf(datum_string, "Unknown ");
	}
	else
	{
		sprintf(datum_string, "WGS-84");
	}

	/* convert from radians to degrees */
	latdeg = R2D * fabs(lat);
	londeg = R2D * fabs(lon);
	pbuf += sprintf(pbuf,
			"\n   Pos: %4d:%09.6f %c %5d:%09.6f %c %10.2f m HAE (%s)",
			(short)latdeg, fmod (latdeg, 1.)*60.0,
			(lat<0.0)?'S':'N',
			(short)londeg, fmod (londeg, 1.)*60.0,
			(lon<0.0)?'W':'E',
			alt,
			datum_string);
	pbuf += sprintf(pbuf,
			"\n   Vel:    %9.3f E       %9.3f N      %9.3f U   (m/sec)",
			vel[0], vel[1], vel[2]);

	pbuf += sprintf(pbuf,
			"\n   SVs: ");
	for (isv = 0; isv < nsvs; isv++) {
		pbuf += sprintf(pbuf, " %02d", sv_prn[isv]);
	}
	pbuf += sprintf(pbuf, "     (IODEs:");
	for (isv = 0; isv < nsvs; isv++) {
		pbuf += sprintf(pbuf, " %02X", sv_IODC[isv]&0xFF);
	}
	pbuf += sprintf(pbuf, ")");
}

/* 0x8F41 */
static void
rpt_8F41(
	 TSIPPKT *rpt
	 )
{
	unsigned char
	    bSearchRange,
	    bBoardOptions,
	    bBuildYear,
	    bBuildMonth,
	    bBuildDay,
	    bBuildHour;
	float
	    fOscOffset;
	unsigned short
	    iTestCodeId;
	unsigned long
	    iiSerialNumber;

	if (!rpt_0x8F41(rpt,
			&bSearchRange,
			&bBoardOptions,
			&iiSerialNumber,
			&bBuildYear,
			&bBuildMonth,
			&bBuildDay,
			&bBuildHour,
			&fOscOffset,
			&iTestCodeId))
	{
		parsed = BADLEN_PARSE;
		return;
	}

	pbuf += sprintf(pbuf, "\n  search range:          %d",
			bSearchRange);
	pbuf += sprintf(pbuf, "\n  board options:         %d",
			bBoardOptions);
	pbuf += sprintf(pbuf, "\n  board serial #:        %ld",
			iiSerialNumber);
	pbuf += sprintf(pbuf, "\n  build date/hour:       %02d/%02d/%02d %02d:00",
			bBuildDay, bBuildMonth, bBuildYear, bBuildHour);
	pbuf += sprintf(pbuf, "\n  osc offset:            %.3f PPM (%.0f Hz)",
			fOscOffset/1575.42, fOscOffset);
	pbuf += sprintf(pbuf, "\n  test code:             %d",
			iTestCodeId);
}

/* 0x8F42 */
static void
rpt_8F42(
	 TSIPPKT *rpt
	 )
{
	unsigned char
	    bProdOptionsPre,
	    bProdNumberExt;
	unsigned short
	    iCaseSerialNumberPre,
	    iPremiumOptions,
	    iMachineID,
	    iKey;
	unsigned long
	    iiCaseSerialNumber,
	    iiProdNumber;

	if (!rpt_0x8F42(rpt,
			&bProdOptionsPre,
			&bProdNumberExt,
			&iCaseSerialNumberPre,
			&iiCaseSerialNumber,
			&iiProdNumber,
			&iPremiumOptions,
			&iMachineID,
			&iKey))
	{
		parsed = BADLEN_PARSE;
		return;
	}

	pbuf += sprintf(pbuf, "\nProduct ID 8F42");
	pbuf += sprintf(pbuf, "\n   extension:            %d", bProdNumberExt);
	pbuf += sprintf(pbuf, "\n   case serial # prefix: %d", iCaseSerialNumberPre);
	pbuf += sprintf(pbuf, "\n   case serial #:        %ld", iiCaseSerialNumber);
	pbuf += sprintf(pbuf, "\n   prod. #:              %ld", iiProdNumber);
	pbuf += sprintf(pbuf, "\n   premium options:      %Xh", iPremiumOptions);
	pbuf += sprintf(pbuf, "\n   machine ID:           %d", iMachineID);
	pbuf += sprintf(pbuf, "\n   key:                  %Xh", iKey);
}

/* 0x8F45 */
static void
rpt_8F45(
	 TSIPPKT *rpt
	 )
{
	unsigned char bSegMask;

	if (!rpt_0x8F45(rpt,
			&bSegMask))
	{
		parsed = BADLEN_PARSE;
		return;
	}
	pbuf += sprintf(pbuf, "\nCleared Segment Mask: %Xh", bSegMask);
}

/* Stinger PPS def */
static void
rpt_8F4A(
	 TSIPPKT *rpt
	 )
{
	unsigned char
	    pps_enabled,
	    pps_timebase,
	    pps_polarity;
	float
	    bias_unc_threshold;
	double
	    pps_offset;

  	if (rpt_0x8F4A_16 (rpt,
			   &pps_enabled,
			   &pps_timebase,
			   &pps_polarity,
			   &pps_offset,
			   &bias_unc_threshold))
	{
		parsed = BADLEN_PARSE;
		return;
	}

	pbuf += sprintf(pbuf, "\nPPS is         %s",	pps_enabled?"enabled":"disabled");
	pbuf += sprintf(pbuf, "\n   timebase:   %s", PPSTimeBaseText[pps_timebase]);
	pbuf += sprintf(pbuf, "\n   polarity:   %s", PPSPolarityText[pps_polarity]);
	pbuf += sprintf(pbuf, "\n   offset:     %.1f ns, ", pps_offset*1.e9);
	pbuf += sprintf(pbuf, "\n   biasunc:    %.1f ns", bias_unc_threshold/GPS_C*1.e9);
}

/* fast-SA decorrolation time for self-survey */
static void
rpt_8F4B(
	 TSIPPKT *rpt
	 )
{
	unsigned long
	    decorr_max;

	if (rpt_0x8F4B(rpt, &decorr_max))
	{
		parsed = BADLEN_PARSE;
		return;
	}

	pbuf += sprintf(pbuf,
			"\nMax # of position fixes for self-survey : %ld",
			decorr_max);
}

static void
rpt_8F4D(
	 TSIPPKT *rpt
	 )
{
	static char
	    *linestart;
	unsigned long
	    OutputMask;
	static unsigned long
	    MaskBit[] = {
		0x00000001, 0x00000002, 0x00000004, 0x00000008, 0x00000010,
		0x00000020,
		0x00000100L, 0x00000800L, 0x00001000L,
		0x40000000L, 0x80000000L};
	int
	    ichoice,
	    numchoices;

	if (rpt_0x8F4D(rpt, &OutputMask))
	{
		parsed = BADLEN_PARSE;
		return;
	}

	pbuf += sprintf(pbuf, "\nAuto-Report Mask: %02X %02X %02X %02X",
			(unsigned char)(OutputMask>>24),
			(unsigned char)(OutputMask>>16),
			(unsigned char)(OutputMask>>8),
			(unsigned char)OutputMask);

	numchoices = sizeof(MaskText)/sizeof(char*);
	pbuf += sprintf(pbuf, "\nAuto-Reports scheduled for Output:");
	linestart = pbuf;
	for (ichoice = 0; ichoice < numchoices; ichoice++)
	{
		if (OutputMask&MaskBit[ichoice])
		{
			pbuf += sprintf(pbuf, "%s %s",
					(pbuf==linestart)?"\n     ":",",
					MaskText[ichoice]);
			if (pbuf-linestart > 60) linestart = pbuf;
		}
	}

	pbuf += sprintf(pbuf, "\nAuto-Reports NOT scheduled for Output:");
	linestart = pbuf;
	for (ichoice = 0; ichoice < numchoices; ichoice++)
	{
		if (OutputMask&MaskBit[ichoice]) continue;
	     	pbuf += sprintf(pbuf, "%s %s",
				(pbuf==linestart)?"\n     ":",",
				MaskText[ichoice]);
		if (pbuf-linestart > 60) linestart = pbuf;
	}
}

static void
rpt_8FA5(
	 TSIPPKT *rpt
	 )
{
	unsigned char
	    spktmask[4];

	if (rpt_0x8FA5(rpt, spktmask))
	{
		parsed = BADLEN_PARSE;
		return;
	}

	pbuf += sprintf(pbuf, "\nSuperpacket auto-output mask: %02X %02X %02X %02X",
			spktmask[0], spktmask[1], spktmask[2], spktmask[3]);

	if (spktmask[0]&0x01) pbuf+= sprintf (pbuf, "\n    PPS   8F-0B");
	if (spktmask[0]&0x02) pbuf+= sprintf (pbuf, "\n    Event 8F-0B");
	if (spktmask[0]&0x10) pbuf+= sprintf (pbuf, "\n    PPS   8F-AD");
	if (spktmask[0]&0x20) pbuf+= sprintf (pbuf, "\n    Event 8F-AD");
	if (spktmask[2]&0x01) pbuf+= sprintf (pbuf, "\n    ppos Fix 8F-20");
}

static void
rpt_8FAD(
	 TSIPPKT *rpt
	 )
{
	unsigned short
	    Count,
	    Year;
	double
	    FracSec;
	unsigned char
	    Hour,
	    Minute,
	    Second,
	    Day,
	    Month,
	    Status,
	    Flags;
	static char* Status8FADText[] = {
		"CODE_DOING_FIXES",
		"CODE_GOOD_1_SV",
		"CODE_APPX_1SV",
		"CODE_NEED_TIME",
		"CODE_NEED_INITIALIZATION",
		"CODE_PDOP_HIGH",
		"CODE_BAD_1SV",
		"CODE_0SVS",
		"CODE_1SV",
		"CODE_2SVS",
		"CODE_3SVS",
		"CODE_NO_INTEGRITY",
		"CODE_DCORR_GEN",
		"CODE_OVERDET_CLK",
		"Invalid Status"},
	    *LeapStatusText[] = {
		    " UTC Avail", " ", " ", " ",
		    " Scheduled", " Pending", " Warning", " In Progress"};
	int i;

	if (rpt_0x8FAD (rpt,
			&Count,
			&FracSec,
			&Hour,
			&Minute,
			&Second,
			&Day,
			&Month,
			&Year,
			&Status,
			&Flags))
	{
		parsed = BADLEN_PARSE;
		return;
	}

	pbuf += sprintf(pbuf,    "\n8FAD   Count: %d   Status: %s",
			Count, Status8FADText[Status]);

	pbuf += sprintf(pbuf, "\n   Leap Flags:");
	if (Flags)
	{
		for (i=0; i<8; i++)
		{
			if (Flags&(1<<i)) pbuf += sprintf(pbuf, LeapStatusText[i]);
		}
	}
	else
	{
		pbuf += sprintf(pbuf, "  UTC info not available");
	}

	pbuf += sprintf(pbuf,     "\n      %02d/%02d/%04d (DMY)  %02d:%02d:%02d.%09ld UTC",
			Day, Month, Year, Hour, Minute, Second, (long)(FracSec*1.e9));
}


int
print_msg_table_header(
		       int rptcode,
		       char *HdrStr,
		       int force
		       )
{
	/* force header is to help auto-output function */
	/* last_rptcode is to determine whether to print a header */
	/* for the first occurrence of a series of reports */
	static int
	    last_rptcode = 0;
	int
	    numchars;

	numchars = 0;
	if (force || rptcode!=last_rptcode)
	{
		/* supply a header in console output */
		switch (rptcode)
		{
		    case 0x5A:
			numchars = sprintf(HdrStr, "\nRaw Measurement Data");
			numchars += sprintf(HdrStr+numchars,
					    "\n   SV  Sample   SNR  Code Phase   Doppler    Seconds     Time of Meas");
			break;

		    case 0x5B:
			numchars = sprintf(HdrStr, "\nEphemeris Status");
			numchars += sprintf(HdrStr+numchars,
					    "\n    SV     Time collected     Health  IODE        t oe         Fit   URA");
			break;

		    case 0x5C:
			numchars = sprintf(HdrStr, "\nTracking Info");
			numchars += sprintf(HdrStr+numchars,
					    "\n   SV  C Acq Eph   SNR     Time of Meas       Elev  Azim   ");
			break;

		}
	}
	last_rptcode = rptcode;
	return (short)numchars;
}

static void
unknown_rpt(
	    TSIPPKT *rpt
	    )
{
	int i;

	/* app-specific rpt packets */
	if (parsed == BADLEN_PARSE)
	{
		pbuf += sprintf(pbuf, "\nTSIP report packet ID %2Xh, length %d: Bad length",
				rpt->code, rpt->len);
	}
	if (parsed == BADID_PARSE)
	{
		pbuf += sprintf(pbuf,
				"\nTSIP report packet ID %2Xh, length %d: translation not supported",
				rpt->code, rpt->len);
	}

	if (parsed == BADDATA_PARSE)
	{
		pbuf += sprintf(pbuf,
				"\nTSIP report packet ID %2Xh, length %d: data content incorrect",
				rpt->code, rpt->len);
	}

	for (i = 0; i < rpt->len; i++) {
		if ((i % 20) == 0) *pbuf++ = '\n';
		pbuf += sprintf(pbuf, " %02X", rpt->buf[i]);
	}
}
/**/

/*
** main subroutine, called from ProcessInputBytesWhileWaitingForKBHit()
*/
void
TranslateTSIPReportToText(
			  TSIPPKT *rpt,
			  char *TextOutputBuffer
			  )
{

	/* pbuf is the pointer to the current location of the text output */
	pbuf = TextOutputBuffer;

	/* keep track of whether the message has been successfully parsed */
	parsed = GOOD_PARSE;

	/* print a header if this is the first of a series of messages */
	pbuf += print_msg_table_header (rpt->code, pbuf, FALSE);

	/* process incoming TSIP report according to code */
	switch (rpt->code)
	{
	    case 0x3D: rpt_chan_A_config (rpt); break;
	    case 0x40: rpt_almanac_data_page (rpt); break;
	    case 0x41: rpt_GPS_time (rpt); break;
	    case 0x42: rpt_single_ECEF_position (rpt); break;
	    case 0x43: rpt_single_ECEF_velocity (rpt); break;
	    case 0x45: rpt_SW_version (rpt); break;
	    case 0x46: rpt_rcvr_health (rpt); break;
	    case 0x47: rpt_SNR_all_SVs (rpt); break;
	    case 0x48: rpt_GPS_system_message (rpt); break;
	    case 0x49: rpt_almanac_health_page (rpt); break;
	    case 0x4A: switch (rpt->len) {
			/*
			** special case (=slip-up) in the TSIP protocol;
			** parsing method depends on length
			*/
		    case 20: rpt_single_lla_position (rpt); break;
		    case  9: rpt_ref_alt (rpt); break;
		} break;
	    case 0x4B: rpt_rcvr_id_and_status (rpt);break;
	    case 0x4C: rpt_operating_parameters (rpt); break;
	    case 0x4D: rpt_oscillator_offset (rpt); break;
	    case 0x4E: rpt_GPS_time_set_response (rpt); break;
	    case 0x4F: rpt_UTC_offset (rpt); break;
	    case 0x54: rpt_1SV_bias (rpt); break;
	    case 0x55: rpt_io_opt (rpt); break;
	    case 0x56: rpt_ENU_velocity (rpt); break;
	    case 0x57: rpt_last_fix_info (rpt); break;
	    case 0x58: rpt_GPS_system_data (rpt); break;
	    case 0x59: rpt_SVs_enabled (rpt); break;
	    case 0x5A: rpt_raw_msmt (rpt); break;
	    case 0x5B: rpt_SV_ephemeris_status (rpt); break;
	    case 0x5C: rpt_SV_tracking_status (rpt); break;
	    case 0x6D: rpt_allSV_selection (rpt); break;
	    case 0x82: rpt_DGPS_position_mode (rpt); break;
	    case 0x83: rpt_double_ECEF_position (rpt); break;
	    case 0x84: rpt_double_lla_position (rpt); break;
	    case 0xBB: rpt_complete_rcvr_config (rpt); break;
	    case 0xBC: rpt_rcvr_serial_port_config (rpt); break;

	    case 0x8F: switch (rpt->buf[0])
		{
			/* superpackets; parsed according to subcodes */
		    case 0x0B: rpt_8F0B(rpt); break;
		    case 0x14: rpt_8F14(rpt); break;
		    case 0x15: rpt_8F15(rpt); break;
		    case 0x20: rpt_8F20(rpt); break;
		    case 0x41: rpt_8F41(rpt); break;
		    case 0x42: rpt_8F42(rpt); break;
		    case 0x45: rpt_8F45(rpt); break;
		    case 0x4A: rpt_8F4A(rpt); break;
		    case 0x4B: rpt_8F4B(rpt); break;
		    case 0x4D: rpt_8F4D(rpt); break;
		    case 0xA5: rpt_8FA5(rpt); break;
		    case 0xAD: rpt_8FAD(rpt); break;
		    default: parsed = BADID_PARSE; break;
		}
		break;

	    default: parsed = BADID_PARSE; break;
	}

	if (parsed != GOOD_PARSE)
	{
		/*
		**The message has TSIP structure (DLEs, etc.)
		** but could not be parsed by above routines
		*/
		unknown_rpt (rpt);
	}

	/* close TextOutputBuffer */
	pbuf = '\0';
}

#endif /* TRIMBLE_OUTPUT_FUNC */

#else  /* defined(REFCLOCK) && defined(CLOCK_RIPENCC) */
int refclock_ripencc_bs;
#endif /* defined(REFCLOCK) && defined(CLOCK_RIPENCC) */

