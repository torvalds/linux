/*****************************************************************************
 *
 *  libntpq.h
 *
 *  This is the wrapper library for ntpq, the NTP query utility. 
 *  This library reuses the sourcecode from ntpq and exports a number
 *  of useful functions in a library that can be linked against applications
 *  that need to query the status of a running ntpd. The whole 
 *  communcation is based on mode 6 packets.
 *
 * This header file can be used in applications that want to link against 
 * libntpq.
 *
 ****************************************************************************/

#include "ntp_net.h"

/* general purpose buffer size */
#define NTPQ_BUFLEN 2048

/* max. number of associations */
#ifndef MAXASSOC
#define MAXASSOC    1024
#endif

/* general purpose max array size definition */
#ifndef MAXLIST
#define MAXLIST 64
#endif

#ifndef LENHOSTNAME
#define LENHOSTNAME 256     /* host name is max. 256 characters long */
#endif

/* NTP Status codes */
#define NTP_STATUS_INVALID      0
#define NTP_STATUS_FALSETICKER  1
#define NTP_STATUS_EXCESS       2
#define NTP_STATUS_OUTLIER      3
#define NTP_STATUS_CANDIDATE    4
#define NTP_STATUS_SELECTED     5
#define NTP_STATUS_SYSPEER      6
#define NTP_STATUS_PPSPEER      7

/* NTP association type identifier */
#define NTP_CLOCKTYPE_UNKNOWN   '-'
#define NTP_CLOCKTYPE_BROADCAST 'b'
#define NTP_CLOCKTYPE_LOCAL     'l'
#define NTP_CLOCKTYPE_UNICAST   'u'
#define NTP_CLOCKTYPE_MULTICAST 'm'

/* Variable Sets */
#define PEERVARS CTL_OP_READVAR
#define CLOCKVARS CTL_OP_CLOCKVAR

/* Variable list struct */
struct ntpq_varlist {
	char *name;
	char *value;
};

/* global variables used for holding snapshots of data */
#ifndef LIBNTPQ_C
extern char peervars[];
extern int peervarlen;
extern int peervar_assoc;
extern char clockvars[];
extern int clockvarlen;
extern int clockvar_assoc;
extern char sysvars[];
extern int sysvarlen;
extern char *ntpq_resultbuffer[];
extern struct ntpq_varlist ntpq_varlist[MAXLIST];
#endif



/* 
 * Prototypes of exported libary functions
 */

/* from libntpq.c */
extern int ntpq_openhost(char *, int);
extern int ntpq_closehost(void);
extern int ntpq_queryhost(unsigned short VARSET, associd_t association,
			  char *resultbuf, int maxlen);
extern size_t ntpq_getvar(const char *resultbuf, size_t datalen,
			  const char *varname, char *varvalue,
			  size_t maxlen);
extern int ntpq_stripquotes ( char *resultbuf, char *srcbuf, int datalen, int maxlen );
extern int ntpq_queryhost_peervars(associd_t association, char *resultbuf, int maxlen);
extern int ntpq_get_peervar( const char *varname, char *varvalue, int maxlen);
extern size_t ntpq_read_sysvars(char *resultbuf, size_t maxsize);
extern int ntpq_get_sysvars( void );
extern int ntpq_read_associations ( unsigned short resultbuf[], int max_entries );
extern int ntpq_get_assocs ( void );
extern int ntpq_get_assoc_number ( associd_t associd );
extern int ntpq_get_assoc_peervars( associd_t associd );
extern int ntpq_get_assoc_clockvars( associd_t associd );
extern int ntpq_get_assoc_allvars( associd_t associd  );
extern int ntpq_get_assoc_clocktype(int assoc_index);
extern int ntpq_read_assoc_peervars( associd_t associd, char *resultbuf, int maxsize );
extern int ntpq_read_assoc_clockvars( associd_t associd, char *resultbuf, int maxsize );

/* in libntpq_subs.c */
extern int ntpq_dogetassoc(void);
extern char ntpq_decodeaddrtype(sockaddr_u *sock);
extern int ntpq_doquerylist(struct ntpq_varlist *, int, associd_t, int,
			    u_short *, size_t *, const char **datap);
