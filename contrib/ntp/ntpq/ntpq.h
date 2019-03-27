/*
 * ntpq.h - definitions of interest to ntpq
 */
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#include "ntp_fp.h"
#include "ntp.h"
#include "ntp_stdlib.h"
#include "ntp_string.h"
#include "ntp_malloc.h"
#include "ntp_assert.h"
#include "ntp_control.h"
#include "lib_strbuf.h"

#include "ntpq-opts.h"

/*
 * Maximum number of arguments
 */
#define	MAXARGS	4

/*
 * Limit on packets in a single response.  Increasing this value to
 * 96 will marginally speed "mrulist" operation on lossless networks
 * but it has been observed to cause loss on WiFi networks and with
 * an IPv6 go6.net tunnel over UDP.  That loss causes the request
 * row limit to be cut in half, and it grows back very slowly to
 * ensure forward progress is made and loss isn't triggered too quickly
 * afterward.  While the lossless case gains only marginally with
 * MAXFRAGS == 96, the lossy case is a lot slower due to the repeated
 * timeouts.  Empirally, MAXFRAGS == 32 avoids most of the routine loss
 * on both the WiFi and UDP v6 tunnel tests and seems a good compromise.
 * This suggests some device in the path has a limit of 32 ~512 byte UDP
 * packets in queue.
 * Lowering MAXFRAGS may help with particularly lossy networks, but some
 * ntpq commands may rely on the longtime value of 24 implicitly,
 * assuming a single multipacket response will be large enough for any
 * needs.  In contrast, the "mrulist" command is implemented as a series
 * of requests and multipacket responses to each.
 */
#define	MAXFRAGS	32

/*
 * Error codes for internal use
 */
#define	ERR_UNSPEC		256
#define	ERR_INCOMPLETE		257
#define	ERR_TIMEOUT		258
#define	ERR_TOOMUCH		259

/*
 * Flags for forming descriptors.
 */
#define	OPT		0x80	/* this argument is optional, or'd with type */

#define	NO		0x0
#define	NTP_STR		0x1	/* string argument */
#define	NTP_UINT	0x2	/* unsigned integer */
#define	NTP_INT		0x3	/* signed integer */
#define	NTP_ADD		0x4	/* IP network address */
#define IP_VERSION	0x5	/* IP version */
#define	NTP_ADP		0x6	/* IP address and port */
#define NTP_LFP		0x7	/* NTP timestamp */
#define NTP_MODE	0x8	/* peer mode */
#define NTP_2BIT	0x9	/* leap bits */

/*
 * Arguments are returned in a union
 */
typedef union {
	const char *string;
	long ival;
	u_long uval;
	sockaddr_u netnum;
} arg_v;

/*
 * Structure for passing parsed command line
 */
struct parse {
	const char *keyword;
	arg_v argval[MAXARGS];
	size_t nargs;
};

/*
 * ntpdc includes a command parser which could charitably be called
 * crude.  The following structure is used to define the command
 * syntax.
 */
struct xcmd {
  const char *keyword;		/* command key word */
	void (*handler)	(struct parse *, FILE *);	/* command handler */
	u_char arg[MAXARGS];	/* descriptors for arguments */
  const char *desc[MAXARGS];	/* descriptions for arguments */
  const char *comment;
};

/*
 * Structure to hold association data
 */
struct association {
	associd_t assid;
	u_short status;
};

/*
 * mrulist terminal status interval
 */
#define	MRU_REPORT_SECS	5

/*
 * var_format is used to override cooked formatting for selected vars.
 */
typedef struct var_format_tag {
	const char *	varname;
	u_short		fmt;
} var_format;

typedef struct chost_tag chost;
struct chost_tag {
	const char *name;
	int 	    fam;
};

extern chost	chosts[];

extern int	interactive;	/* are we prompting? */
extern int	old_rv;		/* use old rv behavior? --old-rv */
extern te_Refid	drefid;		/* How should we display a refid? */
extern u_int	assoc_cache_slots;/* count of allocated array entries */
extern u_int	numassoc;	/* number of cached associations */
extern u_int	numhosts;

extern	void	grow_assoc_cache(void);
extern	void	asciize		(int, char *, FILE *);
extern	int	getnetnum	(const char *, sockaddr_u *, char *, int);
extern	void	sortassoc	(void);
extern	void	show_error_msg	(int, associd_t);
extern	int	dogetassoc	(FILE *);
extern	int	doquery		(int, associd_t, int, size_t, const char *,
				 u_short *, size_t *, const char **);
extern	int	doqueryex	(int, associd_t, int, size_t, const char *,
				 u_short *, size_t *, const char **, int);
extern	const char * nntohost	(sockaddr_u *);
extern	const char * nntohost_col (sockaddr_u *, size_t, int);
extern	const char * nntohostp	(sockaddr_u *);
extern	int	decodets	(char *, l_fp *);
extern	int	decodeuint	(char *, u_long *);
extern	int	nextvar		(size_t *, const char **, char **, char **);
extern	int	decodetime	(char *, l_fp *);
extern	void	printvars	(size_t, const char *, int, int, int, FILE *);
extern	int	decodeint	(char *, long *);
extern	void	makeascii	(size_t, const char *, FILE *);
extern	const char * trunc_left	(const char *, size_t);
extern	const char * trunc_right(const char *, size_t);

typedef	int/*BOOL*/ (*Ctrl_C_Handler)(void);
extern	int/*BOOL*/ 	push_ctrl_c_handler(Ctrl_C_Handler);
extern	int/*BOOL*/ 	pop_ctrl_c_handler(Ctrl_C_Handler);
