/*
 * ntp.h - NTP definitions for the masses
 */
#ifndef NTP_H
#define NTP_H

#include <stddef.h>
#include <math.h>

#include <ntp_fp.h>
#include <ntp_types.h>
#include <ntp_lists.h>
#include <ntp_stdlib.h>
#include <ntp_crypto.h>
#include <ntp_random.h>
#include <ntp_net.h>

#include <isc/boolean.h>

/*
 * Calendar arithmetic - contributed by G. Healton
 */
#define YEAR_BREAK 500		/* years < this are tm_year values:
				 * Break < AnyFourDigitYear && Break >
				 * Anytm_yearYear */

#define YEAR_PIVOT 98		/* 97/98: years < this are year 2000+
				 * FYI: official UNIX pivot year is
				 * 68/69 */

/*
 * Number of Days since 1 BC Gregorian to 1 January of given year
 */
#define julian0(year)	(((year) * 365 ) + ((year) > 0 ? (((year) + 3) \
			    / 4 - ((year - 1) / 100) + ((year - 1) / \
			    400)) : 0))

/*
 * Number of days since start of NTP time to 1 January of given year
 */
#define ntp0(year)	(julian0(year) - julian0(1900))

/*
 * Number of days since start of UNIX time to 1 January of given year
 */
#define unix0(year)	(julian0(year) - julian0(1970))

/*
 * LEAP YEAR test for full 4-digit years (e.g, 1999, 2010)
 */
#define isleap_4(y)	((y) % 4 == 0 && !((y) % 100 == 0 && !(y % \
			    400 == 0)))

/*
 * LEAP YEAR test for tm_year (struct tm) years (e.g, 99, 110)
 */
#define isleap_tm(y)	((y) % 4 == 0 && !((y) % 100 == 0 && !(((y) \
			    + 1900) % 400 == 0)))

/*
 * to convert simple two-digit years to tm_year style years:
 *
 *	if (year < YEAR_PIVOT)
 *		year += 100;
 *
 * to convert either two-digit OR tm_year years to four-digit years:
 *
 *	if (year < YEAR_PIVOT)
 *		year += 100;
 *
 *	if (year < YEAR_BREAK)
 *		year += 1900;
 */

/*
 * How to get signed characters.  On machines where signed char works,
 * use it. On machines where signed char doesn't work, char had better
 * be signed.
 */
#ifdef NEED_S_CHAR_TYPEDEF
# if SIZEOF_SIGNED_CHAR
typedef signed char s_char;
# else
typedef char s_char;
# endif
  /* XXX: Why is this sequent bit INSIDE this test? */
# ifdef sequent
#  undef SO_RCVBUF
#  undef SO_SNDBUF
# endif
#endif

/*
 * NTP protocol parameters.  See section 3.2.6 of the specification.
 */
#define	NTP_VERSION	((u_char)4) /* current version number */
#define	NTP_OLDVERSION	((u_char)1) /* oldest credible version */
#define	NTP_PORT	123	/* included for non-unix machines */

/*
 * Poll interval parameters
 */
#define NTP_UNREACH	10	/* poll unreach threshold */
#define	NTP_MINPOLL	3	/* log2 min poll interval (8 s) */
#define NTP_MINDPOLL	6	/* log2 default min poll (64 s) */
#define NTP_MAXDPOLL	10	/* log2 default max poll (~17 m) */
#define	NTP_MAXPOLL	17	/* log2 max poll interval (~36 h) */
#define	NTP_RETRY	3	/* max packet retries */
#define	NTP_MINPKT	2	/* guard time (s) */

/*
 * Clock filter algorithm tuning parameters
 */
#define MAXDISPERSE	16.	/* max dispersion */
#define	NTP_SHIFT	8	/* clock filter stages */
#define NTP_FWEIGHT	.5	/* clock filter weight */

/*
 * Selection algorithm tuning parameters
 */
#define	NTP_MINCLOCK	3	/* min survivors */
#define	NTP_MAXCLOCK	10	/* max candidates */
#define MINDISPERSE	.001	/* min distance */
#define MAXDISTANCE	1.5	/* max root distance (select threshold) */
#define CLOCK_SGATE	3.	/* popcorn spike gate */
#define HUFFPUFF	900	/* huff-n'-puff sample interval (s) */
#define MAXHOP		2	/* anti-clockhop threshold */
#define MAX_TTL		8	/* max ttl mapping vector size */
#define	BEACON		7200	/* manycast beacon interval */
#define NTP_MAXEXTEN	2048	/* max extension field size */
#define	NTP_ORPHWAIT	300	/* orphan wair (s) */

/*
 * Miscellaneous stuff
 */
#define NTP_MAXKEY	65535	/* max authentication key number */
#define	KEY_TYPE_MD5	NID_md5	/* MD5 digest NID */
/*
 * Limits of things
 */
#define	MAXFILENAME	256	/* max length of file name */
#define MAXHOSTNAME	512	/* max length of host/node name */
#define NTP_MAXSTRLEN	256	/* max string length */

/*
 * Operations for jitter calculations (these use doubles).
 *
 * Note that we carefully separate the jitter component from the
 * dispersion component (frequency error plus precision). The frequency
 * error component is computed as CLOCK_PHI times the difference between
 * the epoch of the time measurement and the reference time. The
 * precision component is computed as the square root of the mean of the
 * squares of a zero-mean, uniform distribution of unit maximum
 * amplitude. Whether this makes statistical sense may be arguable.
 */
#define SQUARE(x) ((x) * (x))
#define SQRT(x) (sqrt(x))
#define DIFF(x, y) (SQUARE((x) - (y)))
#define LOGTOD(a)	ldexp(1., (int)(a)) /* log2 to double */
#define UNIVAR(x)	(SQUARE(.28867513 * LOGTOD(x))) /* std uniform distr */
#define ULOGTOD(a)	ldexp(1., (int)(a)) /* ulog2 to double */

#define	EVENT_TIMEOUT	0	/* one second, that is */


/*
 * The interface structure is used to hold the addresses and socket
 * numbers of each of the local network addresses we are using.
 * Because "interface" is a reserved word in C++ and has so many
 * varied meanings, a change to "endpt" (via typedef) is under way.
 * Eventually the struct tag will change from interface to endpt_tag.
 * endpt is unrelated to the select algorithm's struct endpoint.
 */
typedef struct interface endpt;
struct interface {
	endpt *		elink;		/* endpt list link */
	endpt *		mclink;		/* per-AF_* multicast list */
	void *		ioreg_ctx;	/* IO registration context */
	SOCKET		fd;		/* socket descriptor */
	SOCKET		bfd;		/* for receiving broadcasts */
	u_int32		ifnum;		/* endpt instance count */
	sockaddr_u	sin;		/* unicast address */
	sockaddr_u	mask;		/* subnet mask */
	sockaddr_u	bcast;		/* broadcast address */
	char		name[32];	/* name of interface */
	u_short		family;		/* AF_INET/AF_INET6 */
	u_short		phase;		/* phase in update cycle */
	u_int32		flags;		/* interface flags */
	int		last_ttl;	/* last TTL specified */
	u_int32		addr_refid;	/* IPv4 addr or IPv6 hash */
	int		num_mcast;	/* mcast addrs enabled */
	u_long		starttime;	/* current_time at creation */
	volatile long	received;	/* number of incoming packets */
	long		sent;		/* number of outgoing packets */
	long		notsent;	/* number of send failures */
	u_int		ifindex;	/* for IPV6_MULTICAST_IF */
	isc_boolean_t	ignore_packets; /* listen-read-drop this? */
	struct peer *	peers;		/* list of peers using endpt */
	u_int		peercnt;	/* count of same */
};

/*
 * Flags for interfaces
 */
#define INT_UP		0x001	/* Interface is up */
#define	INT_PPP		0x002	/* Point-to-point interface */
#define	INT_LOOPBACK	0x004	/* the loopback interface */
#define	INT_BROADCAST	0x008	/* can broadcast out this interface */
#define INT_MULTICAST	0x010	/* can multicast out this interface */
#define	INT_BCASTOPEN	0x020	/* broadcast receive socket is open */
#define INT_MCASTOPEN	0x040	/* multicasting enabled */
#define INT_WILDCARD	0x080	/* wildcard interface - usually skipped */
#define INT_MCASTIF	0x100	/* bound directly to MCAST address */
#define INT_PRIVACY	0x200	/* RFC 4941 IPv6 privacy address */
#define INT_BCASTXMIT	0x400   /* socket setup to allow broadcasts */

/*
 * Define flasher bits (tests 1 through 11 in packet procedure)
 * These reveal the state at the last grumble from the peer and are
 * most handy for diagnosing problems, even if not strictly a state
 * variable in the spec. These are recorded in the peer structure.
 *
 * Packet errors
 */
#define TEST1		0X0001	/* duplicate packet */
#define TEST2		0x0002	/* bogus packet */
#define TEST3		0x0004	/* protocol unsynchronized */
#define TEST4		0x0008	/* access denied */
#define TEST5		0x0010	/* bad authentication */
#define TEST6		0x0020	/* bad synch or stratum */
#define TEST7		0x0040	/* bad header */
#define TEST8		0x0080  /* bad autokey */
#define TEST9		0x0100	/* bad crypto */
#define	PKT_TEST_MASK	(TEST1 | TEST2 | TEST3 | TEST4 | TEST5 |\
			TEST6 | TEST7 | TEST8 | TEST9)
/*
 * Peer errors
 */
#define TEST10		0x0200	/* peer bad synch or stratum */
#define	TEST11		0x0400	/* peer distance exceeded */
#define TEST12		0x0800	/* peer synchronization loop */
#define TEST13		0x1000	/* peer unreacable */
#define	PEER_TEST_MASK	(TEST10 | TEST11 | TEST12 | TEST13)

/*
 * Unused flags
 */
#define TEST14		0x2000
#define TEST15		0x4000
#define TEST16		0x8000

/*
 * The peer structure. Holds state information relating to the guys
 * we are peering with. Most of this stuff is from section 3.2 of the
 * spec.
 */
struct peer {
	struct peer *p_link;	/* link pointer in free & peer lists */
	struct peer *adr_link;	/* link pointer in address hash */
	struct peer *aid_link;	/* link pointer in associd hash */
	struct peer *ilink;	/* list of peers for interface */
	sockaddr_u srcadr;	/* address of remote host */
	char *	hostname;	/* if non-NULL, remote name */
	struct addrinfo *addrs;	/* hostname query result */
	struct addrinfo *ai;	/* position within addrs */
	endpt *	dstadr;		/* local address */
	associd_t associd;	/* association ID */
	u_char	version;	/* version number */
	u_char	hmode;		/* local association mode */
	u_char	hpoll;		/* local poll interval */
	u_char	minpoll;	/* min poll interval */
	u_char	maxpoll;	/* max poll interval */
	u_int	flags;		/* association flags */
	u_char	cast_flags;	/* additional flags */
	u_char	last_event;	/* last peer error code */
	u_char	num_events;	/* number of error events */
	u_int32	ttl;		/* ttl/refclock mode */
	char	*ident;		/* group identifier name */

	/*
	 * Variables used by reference clock support
	 */
#ifdef REFCLOCK
	struct refclockproc *procptr; /* refclock structure pointer */
	u_char	refclktype;	/* reference clock type */
	u_char	refclkunit;	/* reference clock unit number */
	u_char	sstclktype;	/* clock type for system status word */
#endif /* REFCLOCK */

	/*
	 * Variables set by received packet
	 */
	u_char	leap;		/* local leap indicator */
	u_char	pmode;		/* remote association mode */
	u_char	stratum;	/* remote stratum */
	u_char	ppoll;		/* remote poll interval */
	s_char	precision;	/* remote clock precision */
	double	rootdelay;	/* roundtrip delay to primary source */
	double	rootdisp;	/* dispersion to primary source */
	u_int32	refid;		/* remote reference ID */
	l_fp	reftime;	/* update epoch */

	/*
	 * Variables used by authenticated client
	 */
	keyid_t keyid;		/* current key ID */
#ifdef AUTOKEY
#define clear_to_zero opcode
	u_int32	opcode;		/* last request opcode */
	associd_t assoc;	/* peer association ID */
	u_int32	crypto;		/* peer status word */
	EVP_PKEY *pkey;		/* public key */
	const EVP_MD *digest;	/* message digest algorithm */
	char	*subject;	/* certificate subject name */
	char	*issuer;	/* certificate issuer name */
	struct cert_info *xinfo; /* issuer certificate */
	keyid_t	pkeyid;		/* previous key ID */
	keyid_t	hcookie;	/* host cookie */
	keyid_t	pcookie;	/* peer cookie */
	const struct pkey_info *ident_pkey; /* identity key */
	BIGNUM	*iffval;	/* identity challenge (IFF, GQ, MV) */
	const BIGNUM *grpkey;	/* identity challenge key (GQ) */
	struct value cookval;	/* receive cookie values */
	struct value recval;	/* receive autokey values */
	struct exten *cmmd;	/* extension pointer */
	u_long	refresh;	/* next refresh epoch */

	/*
	 * Variables used by authenticated server
	 */
	keyid_t	*keylist;	/* session key ID list */
	int	keynumber;	/* current key number */
	struct value encrypt;	/* send encrypt values */
	struct value sndval;	/* send autokey values */
#else	/* !AUTOKEY follows */
#define clear_to_zero status
#endif	/* !AUTOKEY */

	/*
	 * Ephemeral state variables
	 */
	u_char	status;		/* peer status */
	u_char	new_status;	/* under-construction status */
	u_char	reach;		/* reachability register */
	int	flash;		/* protocol error test tally bits */
	u_long	epoch;		/* reference epoch */
	int	burst;		/* packets remaining in burst */
	int	retry;		/* retry counter */
	int	flip;		/* interleave mode control */
	int	filter_nextpt;	/* index into filter shift register */
	double	filter_delay[NTP_SHIFT]; /* delay shift register */
	double	filter_offset[NTP_SHIFT]; /* offset shift register */
	double	filter_disp[NTP_SHIFT]; /* dispersion shift register */
	u_long	filter_epoch[NTP_SHIFT]; /* epoch shift register */
	u_char	filter_order[NTP_SHIFT]; /* filter sort index */
	l_fp	rec;		/* receive time stamp */
	l_fp	xmt;		/* transmit time stamp */
	l_fp	dst;		/* destination timestamp */
	l_fp	aorg;		/* origin timestamp */
	l_fp	borg;		/* alternate origin timestamp */
	l_fp	bxmt;		/* most recent broadcast transmit timestamp */
	double	offset;		/* peer clock offset */
	double	delay;		/* peer roundtrip delay */
	double	jitter;		/* peer jitter (squares) */
	double	disp;		/* peer dispersion */
	double	xleave;		/* interleave delay */
	double	bias;		/* programmed offset bias */

	/*
	 * Variables used to correct for packet length and asymmetry.
	 */
	double	t21;		/* outbound packet delay */
	int	t21_bytes;	/* outbound packet length */
	int	t21_last;	/* last outbound packet length */
	double	r21;		/* outbound data rate */
	double	t34;		/* inbound packet delay */
	int	t34_bytes;	/* inbound packet length */
	double	r34;		/* inbound data rate */

	/*
	 * End of clear-to-zero area
	 */
	u_long	update;		/* receive epoch */
#define end_clear_to_zero update
	int	unreach;	/* watchdog counter */
	int	throttle;	/* rate control */
	u_long	outdate;	/* send time last packet */
	u_long	nextdate;	/* send time next packet */

	/*
	 * Statistic counters
	 */
	u_long	timereset;	/* time stat counters were reset */
	u_long	timelastrec;	/* last packet received time, incl. trash */
	u_long	timereceived;	/* last (clean) packet received time */
	u_long	timereachable;	/* last reachable/unreachable time */

	u_long	sent;		/* packets sent */
	u_long	received;	/* packets received */
	u_long	processed;	/* packets processed */
	u_long	badauth;	/* bad authentication (TEST5) */
	u_long	badNAK;		/* invalid crypto-NAK */
	u_long	bogusorg;	/* bogus origin (TEST2, TEST3) */
	u_long	oldpkt;		/* old duplicate (TEST1) */
	u_long	seldisptoolarge; /* bad header (TEST6, TEST7) */
	u_long	selbroken;	/* KoD received */
};

/*
 * Values for peer.leap, sys_leap
 */
#define	LEAP_NOWARNING	0x0	/* normal, no leap second warning */
#define	LEAP_ADDSECOND	0x1	/* last minute of day has 61 seconds */
#define	LEAP_DELSECOND	0x2	/* last minute of day has 59 seconds */
#define	LEAP_NOTINSYNC	0x3	/* overload, clock is free running */

/*
 * Values for peer mode and packet mode. Only the modes through
 * MODE_BROADCAST and MODE_BCLIENT appear in the transition
 * function. MODE_CONTROL and MODE_PRIVATE can appear in packets,
 * but those never survive to the transition function.
 */
#define	MODE_UNSPEC	0	/* unspecified (old version) */
#define	MODE_ACTIVE	1	/* symmetric active mode */
#define	MODE_PASSIVE	2	/* symmetric passive mode */
#define	MODE_CLIENT	3	/* client mode */
#define	MODE_SERVER	4	/* server mode */
#define	MODE_BROADCAST	5	/* broadcast mode */
/*
 * These can appear in packets
 */
#define	MODE_CONTROL	6	/* control mode */
#define	MODE_PRIVATE	7	/* private mode */
/*
 * This is a made-up mode for broadcast client.
 */
#define	MODE_BCLIENT	6	/* broadcast client mode */

/*
 * Values for peer.stratum, sys_stratum
 */
#define	STRATUM_REFCLOCK ((u_char)0) /* default stratum */
/* A stratum of 0 in the packet is mapped to 16 internally */
#define	STRATUM_PKT_UNSPEC ((u_char)0) /* unspecified in packet */
#define	STRATUM_UNSPEC	((u_char)16) /* unspecified */

/*
 * Values for peer.flags (u_int)
 */
#define	FLAG_CONFIG	0x0001	/* association was configured */
#define	FLAG_PREEMPT	0x0002	/* preemptable association */
#define	FLAG_AUTHENTIC	0x0004	/* last message was authentic */
#define	FLAG_REFCLOCK	0x0008	/* this is actually a reference clock */
#define	FLAG_BC_VOL	0x0010	/* broadcast client volleying */
#define	FLAG_PREFER	0x0020	/* prefer peer */
#define	FLAG_BURST	0x0040	/* burst mode */
#define	FLAG_PPS	0x0080	/* steered by PPS */
#define	FLAG_IBURST	0x0100	/* initial burst mode */
#define	FLAG_NOSELECT	0x0200	/* never select */
#define	FLAG_TRUE	0x0400	/* force truechimer */
#define	FLAG_SKEY	0x0800  /* autokey authentication */
#define	FLAG_XLEAVE	0x1000	/* interleaved protocol */
#define	FLAG_XB		0x2000	/* interleaved broadcast */
#define	FLAG_XBOGUS	0x4000	/* interleaved bogus packet */
#ifdef	OPENSSL
# define FLAG_ASSOC	0x8000	/* autokey request */
#endif /* OPENSSL */
#define FLAG_TSTAMP_PPS	0x10000	/* PPS source provides absolute timestamp */

/*
 * Definitions for the clear() routine.  We use memset() to clear
 * the parts of the peer structure which go to zero.  These are
 * used to calculate the start address and length of the area.
 */
#define	CLEAR_TO_ZERO(p)	((char *)&((p)->clear_to_zero))
#define	END_CLEAR_TO_ZERO(p)	((char *)&((p)->end_clear_to_zero))
#define	LEN_CLEAR_TO_ZERO(p)	(END_CLEAR_TO_ZERO(p) - CLEAR_TO_ZERO(p))
#define CRYPTO_TO_ZERO(p)	((char *)&((p)->clear_to_zero))
#define END_CRYPTO_TO_ZERO(p)	((char *)&((p)->end_clear_to_zero))
#define LEN_CRYPTO_TO_ZERO	(END_CRYPTO_TO_ZERO((struct peer *)0) \
				    - CRYPTO_TO_ZERO((struct peer *)0))

/*
 * Reference clock types.  Added as necessary.
 */
#define	REFCLK_NONE		0	/* unknown or missing */
#define	REFCLK_LOCALCLOCK	1	/* external (e.g., lockclock) */
#define	REFCLK_GPS_TRAK		2	/* TRAK 8810 GPS Receiver */
#define	REFCLK_WWV_PST		3	/* PST/Traconex 1020 WWV/H */
#define	REFCLK_SPECTRACOM	4	/* Spectracom (generic) Receivers */
#define	REFCLK_TRUETIME		5	/* TrueTime (generic) Receivers */
#define REFCLK_IRIG_AUDIO	6	/* IRIG-B/W audio decoder */
#define	REFCLK_CHU_AUDIO	7	/* CHU audio demodulator/decoder */
#define REFCLK_PARSE		8	/* generic driver (usually DCF77,GPS,MSF) */
#define	REFCLK_GPS_MX4200	9	/* Magnavox MX4200 GPS */
#define REFCLK_GPS_AS2201	10	/* Austron 2201A GPS */
#define	REFCLK_GPS_ARBITER	11	/* Arbiter 1088A/B/ GPS */
#define REFCLK_IRIG_TPRO	12	/* KSI/Odetics TPRO-S IRIG */
#define REFCLK_ATOM_LEITCH	13	/* Leitch CSD 5300 Master Clock */
#define REFCLK_MSF_EES		14	/* EES M201 MSF Receiver */
#define	REFCLK_GPSTM_TRUE	15	/* OLD TrueTime GPS/TM-TMD Receiver */
#define REFCLK_IRIG_BANCOMM	16	/* Bancomm GPS/IRIG Interface */
#define REFCLK_GPS_DATUM	17	/* Datum Programmable Time System */
#define REFCLK_ACTS		18	/* Generic Auto Computer Time Service */
#define REFCLK_WWV_HEATH	19	/* Heath GC1000 WWV/WWVH Receiver */
#define REFCLK_GPS_NMEA		20	/* NMEA based GPS clock */
#define REFCLK_GPS_VME		21	/* TrueTime GPS-VME Interface */
#define REFCLK_ATOM_PPS		22	/* 1-PPS Clock Discipline */
#define REFCLK_PTB_ACTS		23	/* replaced by REFCLK_ACTS */
#define REFCLK_USNO		24	/* replaced by REFCLK_ACTS */
#define REFCLK_GPS_HP		26	/* HP 58503A Time/Frequency Receiver */
#define REFCLK_ARCRON_MSF	27	/* ARCRON MSF radio clock. */
#define REFCLK_SHM		28	/* clock attached thru shared memory */
#define REFCLK_PALISADE		29	/* Trimble Navigation Palisade GPS */
#define REFCLK_ONCORE		30	/* Motorola UT Oncore GPS */
#define REFCLK_GPS_JUPITER	31	/* Rockwell Jupiter GPS receiver */
#define REFCLK_CHRONOLOG	32	/* Chrono-log K WWVB receiver */
#define REFCLK_DUMBCLOCK	33	/* Dumb localtime clock */
#define REFCLK_ULINK		34	/* Ultralink M320 WWVB receiver */
#define REFCLK_PCF		35	/* Conrad parallel port radio clock */
#define REFCLK_WWV_AUDIO	36	/* WWV/H audio demodulator/decoder */
#define REFCLK_FG		37	/* Forum Graphic GPS */
#define REFCLK_HOPF_SERIAL	38	/* hopf DCF77/GPS serial receiver  */
#define REFCLK_HOPF_PCI		39	/* hopf DCF77/GPS PCI receiver  */
#define REFCLK_JJY		40	/* JJY receiver  */
#define	REFCLK_TT560		41	/* TrueTime 560 IRIG-B decoder */
#define REFCLK_ZYFER		42	/* Zyfer GPStarplus receiver  */
#define REFCLK_RIPENCC		43	/* RIPE NCC Trimble driver */
#define REFCLK_NEOCLOCK4X	44	/* NeoClock4X DCF77 or TDF receiver */
#define REFCLK_TSYNCPCI		45	/* Spectracom TSYNC PCI timing board */
#define REFCLK_GPSDJSON		46
#define REFCLK_MAX		46


/*
 * NTP packet format.  The mac field is optional.  It isn't really
 * an l_fp either, but for now declaring it that way is convenient.
 * See Appendix A in the specification.
 *
 * Note that all u_fp and l_fp values arrive in network byte order
 * and must be converted (except the mac, which isn't, really).
 */
struct pkt {
	u_char	li_vn_mode;	/* peer leap indicator */
	u_char	stratum;	/* peer stratum */
	u_char	ppoll;		/* peer poll interval */
	s_char	precision;	/* peer clock precision */
	u_fp	rootdelay;	/* roundtrip delay to primary source */
	u_fp	rootdisp;	/* dispersion to primary source*/
	u_int32	refid;		/* reference id */
	l_fp	reftime;	/* last update time */
	l_fp	org;		/* originate time stamp */
	l_fp	rec;		/* receive time stamp */
	l_fp	xmt;		/* transmit time stamp */

#define	MIN_V4_PKT_LEN	(12 * sizeof(u_int32))	/* min header length */
#define	LEN_PKT_NOMAC	(12 * sizeof(u_int32))	/* min header length */
#define	MIN_MAC_LEN	(1 * sizeof(u_int32))	/* crypto_NAK */
#define	MAX_MD5_LEN	(5 * sizeof(u_int32))	/* MD5 */
#define	MAX_MAC_LEN	(6 * sizeof(u_int32))	/* SHA */
#define	KEY_MAC_LEN	sizeof(u_int32)		/* key ID in MAC */
#define	MAX_MDG_LEN	(MAX_MAC_LEN-KEY_MAC_LEN) /* max. digest len */

	/*
	 * The length of the packet less MAC must be a multiple of 64
	 * with an RSA modulus and Diffie-Hellman prime of 256 octets
	 * and maximum host name of 128 octets, the maximum autokey
	 * command is 152 octets and maximum autokey response is 460
	 * octets. A packet can contain no more than one command and one
	 * response, so the maximum total extension field length is 864
	 * octets. But, to handle humungus certificates, the bank must
	 * be broke.
	 *
	 * The different definitions of the 'exten' field are here for
	 * the benefit of applications that want to send a packet from
	 * an auto variable in the stack - not using the AUTOKEY version
	 * saves 2KB of stack space. The receive buffer should ALWAYS be
	 * big enough to hold a full extended packet if the extension
	 * fields have to be parsed or skipped.
	 */
#ifdef AUTOKEY
	u_int32	exten[(NTP_MAXEXTEN + MAX_MAC_LEN) / sizeof(u_int32)];
#else	/* !AUTOKEY follows */
	u_int32	exten[(MAX_MAC_LEN) / sizeof(u_int32)];
#endif	/* !AUTOKEY */
};

/*
 * Stuff for extracting things from li_vn_mode
 */
#define	PKT_MODE(li_vn_mode)	((u_char)((li_vn_mode) & 0x7))
#define	PKT_VERSION(li_vn_mode)	((u_char)(((li_vn_mode) >> 3) & 0x7))
#define	PKT_LEAP(li_vn_mode)	((u_char)(((li_vn_mode) >> 6) & 0x3))

/*
 * Stuff for putting things back into li_vn_mode in packets and vn_mode
 * in ntp_monitor.c's mon_entry.
 */
#define VN_MODE(v, m)		((((v) & 7) << 3) | ((m) & 0x7))
#define	PKT_LI_VN_MODE(l, v, m) ((((l) & 3) << 6) | VN_MODE((v), (m)))


/*
 * Dealing with stratum.  0 gets mapped to 16 incoming, and back to 0
 * on output.
 */
#define	PKT_TO_STRATUM(s)	((u_char)(((s) == (STRATUM_PKT_UNSPEC)) ?\
				(STRATUM_UNSPEC) : (s)))

#define	STRATUM_TO_PKT(s)	((u_char)(((s) == (STRATUM_UNSPEC)) ?\
				(STRATUM_PKT_UNSPEC) : (s)))


/*
 * A test to determine if the refid should be interpreted as text string.
 * This is usually the case for a refclock, which has stratum 0 internally,
 * which results in sys_stratum 1 if the refclock becomes system peer, or
 * in case of a kiss-of-death (KoD) packet that has STRATUM_PKT_UNSPEC (==0)
 * in the packet which is converted to STRATUM_UNSPEC when the packet
 * is evaluated.
 */
#define REFID_ISTEXT(s) (((s) <= 1) || ((s) >= STRATUM_UNSPEC))


/*
 * Event codes. Used for reporting errors/events to the control module
 */
#define	PEER_EVENT	0x080	/* this is a peer event */
#define CRPT_EVENT	0x100	/* this is a crypto event */

/*
 * System event codes
 */
#define	EVNT_UNSPEC	0	/* unspecified */
#define	EVNT_NSET	1	/* freq not set */
#define	EVNT_FSET	2	/* freq set */
#define	EVNT_SPIK	3	/* spike detect */
#define	EVNT_FREQ	4	/* freq mode */
#define	EVNT_SYNC	5	/* clock sync */
#define	EVNT_SYSRESTART	6	/* restart */
#define	EVNT_SYSFAULT	7	/* panic stop */
#define	EVNT_NOPEER	8	/* no sys peer */
#define	EVNT_ARMED	9	/* leap armed */
#define	EVNT_DISARMED	10	/* leap disarmed */
#define	EVNT_LEAP	11	/* leap event */
#define	EVNT_CLOCKRESET	12	/* clock step */
#define	EVNT_KERN	13	/* kernel event */
#define	EVNT_TAI	14	/* TAI */
#define	EVNT_LEAPVAL	15	/* stale leapsecond values */

/*
 * Peer event codes
 */
#define	PEVNT_MOBIL	(1 | PEER_EVENT) /* mobilize */
#define	PEVNT_DEMOBIL	(2 | PEER_EVENT) /* demobilize */
#define	PEVNT_UNREACH	(3 | PEER_EVENT) /* unreachable */
#define	PEVNT_REACH	(4 | PEER_EVENT) /* reachable */
#define	PEVNT_RESTART	(5 | PEER_EVENT) /* restart */
#define	PEVNT_REPLY	(6 | PEER_EVENT) /* no reply */
#define	PEVNT_RATE	(7 | PEER_EVENT) /* rate exceeded */
#define	PEVNT_DENY	(8 | PEER_EVENT) /* access denied */
#define PEVNT_ARMED	(9 | PEER_EVENT) /* leap armed */
#define	PEVNT_NEWPEER	(10 | PEER_EVENT) /* sys peer */
#define	PEVNT_CLOCK	(11 | PEER_EVENT) /* clock event */
#define	PEVNT_AUTH	(12 | PEER_EVENT) /* bad auth */
#define	PEVNT_POPCORN	(13 | PEER_EVENT) /* popcorn */
#define	PEVNT_XLEAVE	(14 | PEER_EVENT) /* interleave mode */
#define	PEVNT_XERR	(15 | PEER_EVENT) /* interleave error */

/*
 * Clock event codes
 */
#define	CEVNT_NOMINAL	0	/* unspecified */
#define	CEVNT_TIMEOUT	1	/* no reply */
#define	CEVNT_BADREPLY	2	/* bad format */
#define	CEVNT_FAULT	3	/* fault */
#define	CEVNT_PROP	4	/* bad signal */
#define	CEVNT_BADDATE	5	/* bad date */
#define	CEVNT_BADTIME	6	/* bad time */
#define CEVNT_MAX	CEVNT_BADTIME

/*
 * Very misplaced value.  Default port through which we send traps.
 */
#define	TRAPPORT	18447


/*
 * To speed lookups, peers are hashed by the low order bits of the
 * remote IP address. These definitions relate to that.
 */
#define	NTP_HASH_SIZE		128
#define	NTP_HASH_MASK		(NTP_HASH_SIZE-1)
#define	NTP_HASH_ADDR(src)	(sock_hash(src) & NTP_HASH_MASK)

/*
 * min, min3 and max.  Makes it easier to transliterate the spec without
 * thinking about it.
 */
#define	min(a,b)	(((a) < (b)) ? (a) : (b))
#define	max(a,b)	(((a) > (b)) ? (a) : (b))
#define	min3(a,b,c)	min(min((a),(b)), (c))


/*
 * Configuration items.  These are for the protocol module (proto_config())
 */
#define	PROTO_BROADCLIENT	1
#define	PROTO_PRECISION		2	/* (not used) */
#define	PROTO_AUTHENTICATE	3
#define	PROTO_BROADDELAY	4
#define	PROTO_AUTHDELAY		5	/* (not used) */
#define PROTO_MULTICAST_ADD	6
#define PROTO_MULTICAST_DEL	7
#define PROTO_NTP		8
#define PROTO_KERNEL		9
#define PROTO_MONITOR		10
#define PROTO_FILEGEN		11
#define	PROTO_PPS		12
#define PROTO_CAL		13
#define PROTO_MINCLOCK		14
#define	PROTO_MAXCLOCK		15
#define PROTO_MINSANE		16
#define PROTO_FLOOR		17
#define PROTO_CEILING		18
#define PROTO_COHORT		19
#define PROTO_CALLDELAY		20
#define PROTO_MINDISP		21
#define PROTO_MAXDIST		22
	/* available		23 */
#define	PROTO_MAXHOP		24
#define	PROTO_BEACON		25
#define	PROTO_ORPHAN		26
#define	PROTO_ORPHWAIT		27
#define	PROTO_MODE7		28
#define	PROTO_UECRYPTO		29
#define	PROTO_UECRYPTONAK	30
#define	PROTO_UEDIGEST		31
#define	PROTO_PCEDIGEST		32
#define	PROTO_BCPOLLBSTEP	33

/*
 * Configuration items for the loop filter
 */
#define	LOOP_DRIFTINIT		1	/* iniitialize frequency */
#define	LOOP_KERN_CLEAR		2	/* set initial frequency offset */
#define LOOP_MAX		3	/* set both step offsets */
#define LOOP_MAX_BACK		4	/* set backward-step offset */
#define LOOP_MAX_FWD		5	/* set forward-step offset */
#define LOOP_PANIC		6	/* set panic offseet */
#define LOOP_PHI		7	/* set dispersion rate */
#define LOOP_MINSTEP		8	/* set step timeout */
#define LOOP_MINPOLL		9	/* set min poll interval (log2 s) */
#define LOOP_ALLAN		10	/* set minimum Allan intercept */
#define LOOP_HUFFPUFF		11	/* set huff-n'-puff filter length */
#define LOOP_FREQ		12	/* set initial frequency */
#define LOOP_CODEC		13	/* set audio codec frequency */
#define	LOOP_LEAP		14	/* insert leap after second 23:59 */
#define	LOOP_TICK		15	/* sim. low precision clock */

/*
 * Configuration items for the stats printer
 */
#define	STATS_FREQ_FILE		1	/* configure drift file */
#define STATS_STATSDIR		2	/* directory prefix for stats files */
#define	STATS_PID_FILE		3	/* configure ntpd PID file */
#define	STATS_LEAP_FILE		4	/* configure ntpd leapseconds file */

#define MJD_1900		15020	/* MJD for 1 Jan 1900 */

/*
 * Default parameters.  We use these in the absence of something better.
 */
#define INADDR_NTP	0xe0000101	/* NTP multicast address 224.0.1.1 */

/*
 * Structure used optionally for monitoring when this is turned on.
 */
typedef struct mon_data	mon_entry;
struct mon_data {
	mon_entry *	hash_next;	/* next structure in hash list */
	DECL_DLIST_LINK(mon_entry, mru);/* MRU list link pointers */
	struct interface * lcladr;	/* address on which this arrived */
	l_fp		first;		/* first time seen */
	l_fp		last;		/* last time seen */
	int		leak;		/* leaky bucket accumulator */
	int		count;		/* total packet count */
	u_short		flags;		/* restrict flags */
	u_char		vn_mode;	/* packet mode & version */
	u_char		cast_flags;	/* flags MDF_?CAST */
	sockaddr_u	rmtadr;		/* address of remote host */
};

/*
 * Values for cast_flags in mon_entry and struct peer.  mon_entry uses
 * only the first three, MDF_UCAST, MDF_MCAST, and MDF_BCAST.
 */
#define	MDF_UCAST	0x01	/* unicast client */
#define	MDF_MCAST	0x02	/* multicast server */
#define	MDF_BCAST	0x04	/* broadcast server */
#define	MDF_POOL	0x08	/* pool client solicitor */
#define MDF_ACAST	0x10	/* manycast client solicitor */
#define	MDF_BCLNT	0x20	/* eph. broadcast/multicast client */
#define MDF_UCLNT	0x40	/* preemptible manycast or pool client */
/*
 * In the context of struct peer in ntpd, three of the cast_flags bits
 * represent configured associations which never receive packets, and
 * whose reach is always 0: MDF_BCAST, MDF_MCAST, and MDF_ACAST.  The
 * last can be argued as responses are received, but those responses do
 * not affect the MDF_ACAST association's reach register, rather they
 * (may) result in mobilizing ephemeral MDF_ACLNT associations.
 */
#define MDF_TXONLY_MASK	(MDF_BCAST | MDF_MCAST | MDF_ACAST | MDF_POOL)
/*
 * manycastclient-like solicitor association cast_flags bits
 */
#define MDF_SOLICIT_MASK	(MDF_ACAST | MDF_POOL)
/*
 * Values used with mon_enabled to indicate reason for enabling monitoring
 */
#define MON_OFF		0x00		/* no monitoring */
#define MON_ON		0x01		/* monitoring explicitly enabled */
#define MON_RES		0x02		/* implicit monitoring for RES_LIMITED */
/*
 * Structure used for restrictlist entries
 */
typedef struct res_addr4_tag {
	u_int32		addr;		/* IPv4 addr (host order) */
	u_int32		mask;		/* IPv4 mask (host order) */
} res_addr4;

typedef struct res_addr6_tag {
	struct in6_addr addr;		/* IPv6 addr (net order) */
	struct in6_addr mask;		/* IPv6 mask (net order) */
} res_addr6;

typedef struct restrict_u_tag	restrict_u;
struct restrict_u_tag {
	restrict_u *	link;		/* link to next entry */
	u_int32		count;		/* number of packets matched */
	u_short		rflags;		/* restrict (accesslist) flags */
	u_short		mflags;		/* match flags */
	short		ippeerlimit;	/* IP peer limit */
	u_long		expire;		/* valid until time */
	union {				/* variant starting here */
		res_addr4 v4;
		res_addr6 v6;
	} u;
};
#define	V4_SIZEOF_RESTRICT_U	(offsetof(restrict_u, u)	\
				 + sizeof(res_addr4))
#define	V6_SIZEOF_RESTRICT_U	(offsetof(restrict_u, u)	\
				 + sizeof(res_addr6))

typedef struct r4addr_tag	r4addr;
struct r4addr_tag {
	u_short		rflags;		/* match flags */
	short		ippeerlimit;	/* IP peer limit */
};

char *build_iflags(u_int32 flags);
char *build_mflags(u_short mflags);
char *build_rflags(u_short rflags);

/*
 * Restrict (Access) flags (rflags)
 */
#define	RES_IGNORE		0x0001	/* ignore packet */
#define	RES_DONTSERVE		0x0002	/* access denied */
#define	RES_DONTTRUST		0x0004	/* authentication required */
#define	RES_VERSION		0x0008	/* version mismatch */
#define	RES_NOPEER		0x0010	/* new association denied */
#define	RES_NOEPEER		0x0020	/* new ephemeral association denied */
#define RES_LIMITED		0x0040	/* packet rate exceeded */
#define RES_FLAGS		(RES_IGNORE | RES_DONTSERVE |\
				    RES_DONTTRUST | RES_VERSION |\
				    RES_NOPEER | RES_NOEPEER | RES_LIMITED)

#define	RES_NOQUERY		0x0080	/* mode 6/7 packet denied */
#define	RES_NOMODIFY		0x0100	/* mode 6/7 modify denied */
#define	RES_NOTRAP		0x0200	/* mode 6/7 set trap denied */
#define	RES_LPTRAP		0x0400	/* mode 6/7 low priority trap */

#define	RES_KOD			0x0800	/* send kiss of death packet */
#define	RES_MSSNTP		0x1000	/* enable MS-SNTP authentication */
#define	RES_FLAKE		0x2000	/* flakeway - drop 10% */
#define	RES_NOMRULIST		0x4000	/* mode 6 mrulist denied */
#define RES_UNUSED		0x8000	/* Unused flag bits */

#define	RES_ALLFLAGS		(RES_FLAGS | RES_NOQUERY |	\
				 RES_NOMODIFY | RES_NOTRAP |	\
				 RES_LPTRAP | RES_KOD |		\
				 RES_MSSNTP | RES_FLAKE |	\
				 RES_NOMRULIST)

/*
 * Match flags (mflags)
 */
#define	RESM_INTERFACE		0x1000	/* this is an interface */
#define	RESM_NTPONLY		0x2000	/* match source port 123 */
#define RESM_SOURCE		0x4000	/* from "restrict source" */

/*
 * Restriction configuration ops
 */
typedef enum
restrict_ops {
	RESTRICT_FLAGS = 1,	/* add rflags to restrict entry */
	RESTRICT_UNFLAG,	/* remove rflags from restrict entry */
	RESTRICT_REMOVE,	/* remove a restrict entry */
	RESTRICT_REMOVEIF,	/* remove an interface restrict entry */
} restrict_op;

/*
 * Endpoint structure for the select algorithm
 */
struct endpoint {
	double	val;			/* offset of endpoint */
	int	type;			/* interval entry/exit */
};

/*
 * Association matching AM[] return codes
 */
#define AM_ERR		-1		/* error */
#define AM_NOMATCH	0		/* no match */
#define AM_PROCPKT	1		/* server/symmetric packet */
#define AM_BCST		2		/* broadcast packet */
#define AM_FXMIT	3		/* client packet */
#define AM_MANYCAST	4		/* manycast or pool */
#define AM_NEWPASS	5		/* new passive */
#define AM_NEWBCL	6		/* new broadcast */
#define AM_POSSBCL	7		/* discard broadcast */

/* NetInfo configuration locations */
#ifdef HAVE_NETINFO
#define NETINFO_CONFIG_DIR "/config/ntp"
#endif

/* ntpq -c mrulist rows per request limit in ntpd */
#define MRU_ROW_LIMIT	256
/* similar datagrams per response limit for ntpd */
#define MRU_FRAGS_LIMIT	128
#endif /* NTP_H */
