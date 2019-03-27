/* @(#) $Header$ (LBL) */

/*
 * Rockwell Jupiter GPS receiver definitions
 *
 * This is all based on the "Zodiac GPS Receiver Family Designer's
 * Guide" (dated 12/96)
 */

#define JUPITER_SYNC		0x81ff	/* sync word (book says 0xff81 !?!?) */
#define JUPITER_ALL		0xffff	/* disable all output messages */

/* Output messages (sent by the Jupiter board) */
#define JUPITER_O_GPOS		1000	/* geodetic position status */
#define JUPITER_O_EPOS		1001	/* ECEF position status */
#define JUPITER_O_CHAN		1002	/* channel summary */
#define JUPITER_O_VIS		1003	/* visible satellites */
#define JUPITER_O_DGPS		1005	/* differential GPS status */
#define JUPITER_O_MEAS		1007	/* channel measurement */
#define JUPITER_O_ID		1011	/* receiver id */
#define JUPITER_O_USER		1012	/* user-settings output */
#define JUPITER_O_TEST		1100	/* built-in test results */
#define JUPITER_O_MARK		1102	/* measurement time mark */
#define JUPITER_O_PULSE		1108	/* UTC time mark pulse output */
#define JUPITER_O_PORT		1130	/* serial port com parameters in use */
#define JUPITER_O_EUP		1135	/* EEPROM update */
#define JUPITER_O_ESTAT		1136	/* EEPROM status */

/* Input messages (sent to the Jupiter board) */
#define JUPITER_I_PVTINIT	1200	/* geodetic position and velocity */
#define JUPITER_I_USER		1210	/* user-defined datum */
#define JUPITER_I_MAPSEL	1211	/* map datum select */
#define JUPITER_I_ELEV		1212	/* satellite elevation mask control */
#define JUPITER_I_CAND		1213	/* satellite candidate select */
#define JUPITER_I_DGPS		1214	/* differential GPS control */
#define JUPITER_I_COLD		1216	/* cold start control */
#define JUPITER_I_VALID		1217	/* solution validity criteria */
#define JUPITER_I_ALT		1219	/* user-entered altitude input */
#define JUPITER_I_PLAT		1220	/* application platform control */
#define JUPITER_I_NAV		1221	/* nav configuration */
#define JUPITER_I_TEST		1300	/* preform built-in test command */
#define JUPITER_I_RESTART	1303	/* restart command */
#define JUPITER_I_PORT		1330	/* serial port com parameters */
#define JUPITER_I_PROTO		1331	/* message protocol control */
#define JUPITER_I_RDGPS		1351	/* raw DGPS RTCM SC-104 data */

struct jheader {
	u_short sync;		/* (JUPITER_SYNC) */
	u_short id;		/* message id */
	u_short len;		/* number of data short wordss (w/o cksum) */
	u_char reqid;		/* JUPITER_REQID_MASK bits available as id */
	u_char flags;		/* flags */
	u_short hsum;		/* header cksum */
};

#define JUPITER_REQID_MASK	0x3f	/* bits available as id */
#define JUPITER_FLAG_NAK	0x01	/* negative acknowledgement */
#define JUPITER_FLAG_ACK	0x02	/* acknowledgement */
#define JUPITER_FLAG_REQUEST	0x04	/* request ACK or NAK */
#define JUPITER_FLAG_QUERY	0x08	/* request one shot output message */
#define JUPITER_FLAG_LOG	0x20	/* request periodic output message */
#define JUPITER_FLAG_CONN	0x40	/* enable periodic message */
#define JUPITER_FLAG_DISC	0x80	/* disable periodic message */

#define JUPITER_H_FLAG_BITS \
    "\020\1NAK\2ACK\3REQUEST\4QUERY\5MBZ\6LOG\7CONN\10DISC"

/* Log request messages (data payload when using JUPITER_FLAG_LOG) */
struct jrequest {
	u_short trigger;		/* if 0, trigger on time trigger on
					   update (e.g. new almanac) */
	u_short interval;		/* frequency in seconds */
	u_short offset;			/* offset into minute */
	u_short dsum;			/* checksum */
};

/* JUPITER_O_GPOS (1000) */
struct jgpos {
	u_short stime[2];		/* set time (10 ms ticks) */
	u_short seq;			/* sequence number */
	u_short sseq;			/* sat measurement sequence number */
	u_short navval;			/* navigation soltuion validity */
	u_short navtype;		/* navigation solution type */
	u_short nmeas;			/* # of measurements used in solution */
	u_short polar;			/* if 1 then polar navigation */
	u_short gweek;			/* GPS week number */
	u_short sweek[2];		/* GPS seconds into week */
	u_short nsweek[2];		/* GPS nanoseconds into second */
	u_short utcday;			/* 1 to 31 */
	u_short utcmon;			/* 1 to 12 */
	u_short utcyear;		/* 1980 to 2079 */
	u_short utchour;		/* 0 to 23 */
	u_short utcmin;			/* 0 to 59 */
	u_short utcsec;			/* 0 to 59 */
	u_short utcnsec[2];		/* 0 to 999999999 */
	u_short lat[2];			/* latitude (radians) */
	u_short lon[2];			/* longitude (radians) */
	u_short height[2];		/* height (meters) */
	u_short gsep;			/* geoidal separation */
	u_short speed[2];		/* ground speed (meters/sec) */
	u_short course;			/* true course (radians) */
	u_short mvar;
	u_short climb;
	u_short mapd;
	u_short herr[2];
	u_short verr[2];
	u_short terr[2];
	u_short hverr;
	u_short bias[2];
	u_short biassd[2];
	u_short drift[2];
	u_short driftsd[2];
	u_short dsum;			/* checksum */
};
#define JUPITER_O_GPOS_NAV_NOALT	0x01	/* altitude used */
#define JUPITER_O_GPOS_NAV_NODGPS	0x02	/* no differential GPS */
#define JUPITER_O_GPOS_NAV_NOSAT	0x04	/* not enough satellites */
#define JUPITER_O_GPOS_NAV_MAXH		0x08	/* exceeded max EHPE */
#define JUPITER_O_GPOS_NAV_MAXV		0x10	/* exceeded max EVPE */

/* JUPITER_O_CHAN (1002) */
struct jchan {
	u_short stime[2];		/* set time (10 ms ticks) */
	u_short seq;			/* sequence number */
	u_short sseq;			/* sat measurement sequence number */
	u_short gweek;			/* GPS week number */
	u_short sweek[2];		/* GPS seconds into week */
	u_short gpsns[2];		/* GPS nanoseconds from epoch */
	struct jchan2 {
		u_short flags;		/* flags */
		u_short prn;		/* satellite PRN */
		u_short chan;		/* channel number */
	} sat[12];
	u_short dsum;
};

/* JUPITER_O_VIS (1003) */
struct jvis {
	u_short stime[2];		/* set time (10 ms ticks) */
	u_short seq;			/* sequence number */
	u_short gdop;			/* best possible GDOP */
	u_short pdop;			/* best possible PDOP */
	u_short hdop;			/* best possible HDOP */
	u_short vdop;			/* best possible VDOP */
	u_short tdop;			/* best possible TDOP */
	u_short nvis;			/* number of visible satellites */
	struct jvis2 {
		u_short prn;		/* satellite PRN */
		u_short azi;		/* satellite azimuth (radians) */
		u_short elev;		/* satellite elevation (radians) */
	} sat[12];
	u_short dsum;			/* checksum */
};

/* JUPITER_O_ID (1011) */
struct jid {
	u_short stime[2];		/* set time (10 ms ticks) */
	u_short seq;			/* sequence number */
	char chans[20];			/* number of channels (ascii) */
	char vers[20];			/* software version (ascii) */
	char date[20];			/* software date (ascii) */
	char opts[20];			/* software options (ascii) */
	char reserved[20];
	u_short dsum;			/* checksum */
};

/* JUPITER_O_USER (1012) */
struct juser {
	u_short stime[2];		/* set time (10 ms ticks) */
	u_short seq;			/* sequence number */
	u_short status;			/* operatinoal status */
	u_short coldtmo;		/* cold start time-out */
	u_short dgpstmo;		/* DGPS correction time-out*/
	u_short emask;			/* elevation mask */
	u_short selcand[2];		/* selected candidate */
	u_short solflags;		/* solution validity criteria */
	u_short nsat;			/* number of satellites in track */
	u_short herr[2];		/* minimum expected horizontal error */
	u_short verr[2];		/* minimum expected vertical error */
	u_short platform;		/* application platform */
	u_short dsum;			/* checksum */
};

/* JUPITER_O_PULSE (1108) */
struct jpulse {
	u_short stime[2];		/* set time (10 ms ticks) */
	u_short seq;			/* sequence number */
	u_short reserved[5];
	u_short sweek[2];		/* GPS seconds into week */
	short offs;			/* GPS to UTC time offset (seconds) */
	u_short offns[2];		/* GPS to UTC offset (nanoseconds) */
	u_short flags;			/* flags */
	u_short dsum;			/* checksum */
};
#define JUPITER_O_PULSE_VALID		0x1	/* time mark validity */
#define JUPITER_O_PULSE_UTC		0x2	/* GPS/UTC sync */

/* JUPITER_O_EUP (1135) */
struct jeup {
	u_short stime[2];		/* set time (10 ms ticks) */
	u_short seq;			/* sequence number */
	u_char dataid;			/* data id */
	u_char prn;			/* satellite PRN */
	u_short dsum;			/* checksum */
};

/* JUPITER_I_RESTART (1303) */
struct jrestart {
	u_short seq;			/* sequence number */
	u_short flags;
	u_short dsum;			/* checksum */
};
#define JUPITER_I_RESTART_INVRAM	0x01
#define JUPITER_I_RESTART_INVEEPROM	0x02
#define JUPITER_I_RESTART_INVRTC	0x04
#define JUPITER_I_RESTART_COLD		0x80

/* JUPITER_I_PVTINIT (1200) */
struct jpvtinit {
	u_short flags;
	u_short gweek;			/* GPS week number */
	u_short sweek[2];		/* GPS seconds into week */
	u_short utcday;			/* 1 to 31 */
	u_short utcmon;			/* 1 to 12 */
	u_short utcyear;		/* 1980 to 2079 */
	u_short utchour;		/* 0 to 23 */
	u_short utcmin;			/* 0 to 59 */
	u_short utcsec;			/* 0 to 59 */
	u_short lat[2];			/* latitude (radians) */
	u_short lon[2];			/* longitude (radians) */
	u_short height[2];		/* height (meters) */
	u_short speed[2];		/* ground speed (meters/sec) */
	u_short course;			/* true course (radians) */
	u_short climb;
	u_short dsum;
};
#define JUPITER_I_PVTINIT_FORCE		0x01
#define JUPITER_I_PVTINIT_GPSVAL	0x02
#define JUPITER_I_PVTINIT_UTCVAL	0x04
#define JUPITER_I_PVTINIT_POSVAL	0x08
#define JUPITER_I_PVTINIT_ALTVAL	0x10
#define JUPITER_I_PVTINIT_SPDVAL	0x12
#define JUPITER_I_PVTINIT_MAGVAL	0x14
#define JUPITER_I_PVTINIT_CLIMBVAL	0x18

/* JUPITER_I_PLAT (1220) */
struct jplat {
	u_short seq;			/* sequence number */
	u_short platform;		/* application platform */
	u_short dsum;
};
#define JUPITER_I_PLAT_DEFAULT		0	/* default dynamics */
#define JUPITER_I_PLAT_LOW		2	/* pedestrian */
#define JUPITER_I_PLAT_MED		5	/* land (e.g. automobile) */
#define JUPITER_I_PLAT_HIGH		6	/* air */
