/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * @(#)ip_fil.h	1.35 6/5/96
 * $Id$
 */

typedef struct ipmon_msg_s {
	int	imm_msglen;
	char	*imm_msg;
	int	imm_dsize;
	void	*imm_data;
	time_t	imm_when;
	int	imm_loglevel;
} ipmon_msg_t;

typedef	void	(*ims_destroy_func_t)(void *);
typedef	void	*(*ims_dup_func_t)(void *);
typedef	int	(*ims_match_func_t)(void *, void *);
typedef	void	*(*ims_parse_func_t)(char **);
typedef	void	(*ims_print_func_t)(void *);
typedef	int	(*ims_store_func_t)(void *, ipmon_msg_t *);

typedef struct ipmon_saver_s {
	char			*ims_name;
	ims_destroy_func_t	ims_destroy;
	ims_dup_func_t		ims_dup;
	ims_match_func_t	ims_match;
	ims_parse_func_t	ims_parse;
	ims_print_func_t	ims_print;
	ims_store_func_t	ims_store;
} ipmon_saver_t;

typedef struct	ipmon_saver_int_s {
	struct ipmon_saver_int_s	*imsi_next;
	ipmon_saver_t			*imsi_stor;
	void				*imsi_handle;
} ipmon_saver_int_t;

typedef	struct	ipmon_doing_s {
	struct ipmon_doing_s	*ipmd_next;
	void			*ipmd_token;
	ipmon_saver_t		*ipmd_saver;
	/*
	 * ipmd_store is "cached" in this structure to avoid a double
	 * deref when doing saves....
	 */
	int			(*ipmd_store)(void *, ipmon_msg_t *);
} ipmon_doing_t;


typedef	struct	ipmon_action {
	struct	ipmon_action	*ac_next;
	int	ac_mflag;	/* collection of things to compare */
	int	ac_dflag;	/* flags to compliment the doing fields */
	int	ac_logpri;
	int	ac_direction;
	char	ac_group[FR_GROUPLEN];
	char	ac_nattag[16];
	u_32_t	ac_logtag;
	int	ac_type;	/* nat/state/ipf */
	int	ac_proto;
	int	ac_rule;
	int	ac_packet;
	int	ac_second;
	int	ac_result;
	u_32_t	ac_sip;
	u_32_t	ac_smsk;
	u_32_t	ac_dip;
	u_32_t	ac_dmsk;
	u_short	ac_sport;
	u_short	ac_dport;
	char	*ac_iface;
	/*
	 * used with ac_packet/ac_second
	 */
	struct	timeval	ac_last;
	int	ac_pktcnt;
	/*
	 * What to do with matches
	 */
	ipmon_doing_t	*ac_doing;
} ipmon_action_t;

#define	ac_lastsec	ac_last.tv_sec
#define	ac_lastusec	ac_last.tv_usec

/*
 * Flags indicating what fields to do matching upon (ac_mflag).
 */
#define	IPMAC_DIRECTION	0x0001
#define	IPMAC_DSTIP	0x0002
#define	IPMAC_DSTPORT	0x0004
#define	IPMAC_EVERY	0x0008
#define	IPMAC_GROUP	0x0010
#define	IPMAC_INTERFACE	0x0020
#define	IPMAC_LOGTAG	0x0040
#define	IPMAC_NATTAG	0x0080
#define	IPMAC_PROTOCOL	0x0100
#define	IPMAC_RESULT	0x0200
#define	IPMAC_RULE	0x0400
#define	IPMAC_SRCIP	0x0800
#define	IPMAC_SRCPORT	0x1000
#define	IPMAC_TYPE	0x2000
#define	IPMAC_WITH	0x4000

#define	IPMR_BLOCK	1
#define	IPMR_PASS	2
#define	IPMR_NOMATCH	3
#define	IPMR_LOG	4

#define	IPMON_SYSLOG	0x001
#define	IPMON_RESOLVE	0x002
#define	IPMON_HEXBODY	0x004
#define	IPMON_HEXHDR	0x010
#define	IPMON_TAIL	0x020
#define	IPMON_VERBOSE	0x040
#define	IPMON_NAT	0x080
#define	IPMON_STATE	0x100
#define	IPMON_FILTER	0x200
#define	IPMON_PORTNUM	0x400
#define	IPMON_LOGALL	(IPMON_NAT|IPMON_STATE|IPMON_FILTER)
#define	IPMON_LOGBODY	0x800

#define	HOSTNAME_V4(a,b)	hostname((a), 4, (u_32_t *)&(b))

#ifndef	LOGFAC
#define	LOGFAC	LOG_LOCAL0
#endif

extern	void	dump_config __P((void));
extern	int	load_config __P((char *));
extern	void	unload_config __P((void));
extern	void	dumphex __P((FILE *, int, char *, int));
extern	int	check_action __P((char *, char *, int, int));
extern	char	*getword __P((int));
extern	void	*add_doing __P((ipmon_saver_t *));

