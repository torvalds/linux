/*
 * Copyright (C) 2002-2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 */
#if defined(KERNEL) || defined(_KERNEL)
# undef KERNEL
# undef _KERNEL
# define        KERNEL	1
# define        _KERNEL	1
#endif
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/file.h>
#if defined(__FreeBSD_version) && defined(_KERNEL)
# include <sys/fcntl.h>
# include <sys/filio.h>
#else
# include <sys/ioctl.h>
#endif
#if !defined(_KERNEL)
# include <string.h>
# define _KERNEL
# include <sys/uio.h>
# undef _KERNEL
#endif
#include <sys/socket.h>
#include <net/if.h>
#if defined(__FreeBSD__)
#  include <sys/cdefs.h>
#  include <sys/proc.h>
#endif
#if defined(_KERNEL)
# include <sys/systm.h>
# if !defined(__SVR4)
#  include <sys/mbuf.h>
# endif
#endif
#include <netinet/in.h>

#include "netinet/ip_compat.h"
#include "netinet/ip_fil.h"
#include "netinet/ip_pool.h"
#include "netinet/ip_htable.h"
#include "netinet/ip_lookup.h"
#include "netinet/ip_nat.h"
#include "netinet/ip_state.h"
#include "netinet/ip_proxy.h"
#include "netinet/ip_auth.h"
/* END OF INCLUDES */

/*
 * NetBSD has moved to 64bit time_t for all architectures.
 * For some, such as sparc64, there is no change because long is already
 * 64bit, but for others (i386), there is...
 */
#ifdef IPFILTER_COMPAT

# ifdef __NetBSD__
typedef struct timeval_l {
	long	tv_sec;
	long	tv_usec;
} timeval_l_t;
# endif

/* ------------------------------------------------------------------------ */

typedef struct tcpinfo4 {
	u_short		ts_sport;
	u_short		ts_dport;
	tcpdata_t	ts_data[2];
} tcpinfo4_t;

static void ipf_v5tcpinfoto4 __P((tcpinfo_t *, tcpinfo4_t *));

static void
ipf_v5tcpinfoto4(v5, v4)
	tcpinfo_t *v5;
	tcpinfo4_t *v4;
{
	v4->ts_sport = v5->ts_sport;
	v4->ts_dport = v5->ts_dport;
	v4->ts_data[0] = v5->ts_data[0];
	v4->ts_data[1] = v5->ts_data[1];
}

typedef struct	fr_ip4	{
	u_32_t	fi_v:4;
	u_32_t	fi_xx:4;
	u_32_t	fi_tos:8;
	u_32_t	fi_ttl:8;
	u_32_t	fi_p:8;
	u_32_t	fi_optmsk;
	i6addr_t fi_src;
	i6addr_t fi_dst;
	u_short	ofi_secmsk;
	u_short	ofi_auth;
	u_32_t	fi_flx;
	u_32_t	fi_tcpmsk;
	u_32_t	fi_res1;
} frip4_t;

typedef struct	frpcmp4	{
	int	frp_cmp;
	u_short	frp_port;
	u_short	frp_top;
} frpcmp4_t;

typedef	struct	frtuc4	{
	u_char	ftu_tcpfm;
	u_char	ftu_tcpf;
	frpcmp4_t	ftu_src;
	frpcmp4_t	ftu_dst;
} frtuc4_t;

typedef	struct	fripf4	{
	frip4_t fri_ip;
	frip4_t fri_mip;

	u_short	fri_icmpm;
	u_short	fri_icmp;

	frtuc4_t	fri_tuc;
	int	fri_satype;
	int	fri_datype;
	int	fri_sifpidx;
	int	fri_difpidx;
} fripf4_t;

typedef struct frdest_4 {
	void		*fd_ifp;
	i6addr_t	ofd_ip6;
	char		fd_ifname[LIFNAMSIZ];
} frdest_4_t;

/* ------------------------------------------------------------------------ */

/* 5.1.0 new release (current)
 * 4.1.34 changed the size of the time structure used for pps
 * 4.1.16 moved the location of fr_flineno
 * 4.1.0 base version
 */
typedef	struct	frentry_4_1_34 {
	ipfmutex_t	fr_lock;
	struct	frentry	*fr_next;
	struct	frentry	**fr_grp;
	struct	ipscan	*fr_isc;
	void	*fr_ifas[4];
	void	*fr_ptr;	/* for use with fr_arg */
	char	*fr_comment;	/* text comment for rule */
	int	fr_ref;		/* reference count - for grouping */
	int	fr_statecnt;	/* state count - for limit rules */
	int	fr_flineno;	/* line number from conf file */
	U_QUAD_T	fr_hits;
	U_QUAD_T	fr_bytes;
	union {
		struct timeval	frp_lastpkt;
		char	frp_bytes[12];
	} fr_lpu;
	int		fr_curpps;
	union	{
		void		*fru_data;
		char		*fru_caddr;
		fripf4_t	*fru_ipf;
		frentfunc_t	fru_func;
	} fr_dun;
	ipfunc_t fr_func; 	/* call this function */
	int	fr_dsize;
	int	fr_pps;
	int	fr_statemax;	/* max reference count */
	u_32_t	fr_type;
	u_32_t	fr_flags;	/* per-rule flags && options (see below) */
	u_32_t	fr_logtag;	/* user defined log tag # */
	u_32_t	fr_collect;	/* collection number */
	u_int	fr_arg;		/* misc. numeric arg for rule */
	u_int	fr_loglevel;	/* syslog log facility + priority */
	u_int	fr_age[2];	/* non-TCP timeouts */
	u_char	fr_v;
	u_char	fr_icode;	/* return ICMP code */
	char	fr_group[FR_GROUPLEN];	/* group to which this rule belongs */
	char	fr_grhead[FR_GROUPLEN];	/* group # which this rule starts */
	ipftag_t fr_nattag;
	char	fr_ifnames[4][LIFNAMSIZ];
	char	fr_isctag[16];
	frdest_4_t fr_tifs[2];	/* "to"/"reply-to" interface */
	frdest_4_t fr_dif;	/* duplicate packet interface */
	u_int	fr_cksum;	/* checksum on filter rules for performance */
} frentry_4_1_34_t;

typedef	struct	frentry_4_1_16 {
	ipfmutex_t	fr_lock;
	struct	frentry	*fr_next;
	struct	frentry	**fr_grp;
	struct	ipscan	*fr_isc;
	void	*fr_ifas[4];
	void	*fr_ptr;
	char	*fr_comment;
	int	fr_ref;
	int	fr_statecnt;
	int	fr_flineno;
	U_QUAD_T	fr_hits;
	U_QUAD_T	fr_bytes;
	union {
#ifdef __NetBSD__
		timeval_l_t	frp_lastpkt;
#else
		struct timeval	frp_lastpkt;
#endif
	} fr_lpu;
	int		fr_curpps;
	union	{
		void		*fru_data;
		caddr_t		fru_caddr;
		fripf4_t	*fru_ipf;
		frentfunc_t	fru_func;
	} fr_dun;
	ipfunc_t fr_func;
	int	fr_dsize;
	int	fr_pps;
	int	fr_statemax;
	u_32_t	fr_type;
	u_32_t	fr_flags;
	u_32_t	fr_logtag;
	u_32_t	fr_collect;
	u_int	fr_arg;
	u_int	fr_loglevel;
	u_int	fr_age[2];
	u_char	fr_v;
	u_char	fr_icode;
	char	fr_group[FR_GROUPLEN];
	char	fr_grhead[FR_GROUPLEN];
	ipftag_t fr_nattag;
	char	fr_ifnames[4][LIFNAMSIZ];
	char	fr_isctag[16];
	frdest_4_t fr_tifs[2];
	frdest_4_t fr_dif;
	u_int	fr_cksum;
} frentry_4_1_16_t;

typedef	struct	frentry_4_1_0 {
	ipfmutex_t	fr_lock;
	struct	frentry	*fr_next;
	struct	frentry	**fr_grp;
	struct	ipscan	*fr_isc;
	void	*fr_ifas[4];
	void	*fr_ptr;
	char	*fr_comment;
	int	fr_ref;
	int	fr_statecnt;
	U_QUAD_T	fr_hits;
	U_QUAD_T	fr_bytes;
	union {
#ifdef __NetBSD__
		timeval_l_t	frp_lastpkt;
#else
		struct timeval	frp_lastpkt;
#endif
	} fr_lpu;
	int		fr_curpps;

	union	{
		void		*fru_data;
		caddr_t		fru_caddr;
		fripf4_t	*fru_ipf;
		frentfunc_t	fru_func;
	} fr_dun;
	/*
	 * Fields after this may not change whilst in the kernel.
	 */
	ipfunc_t fr_func;
	int	fr_dsize;
	int	fr_pps;
	int	fr_statemax;
	int	fr_flineno;
	u_32_t	fr_type;
	u_32_t	fr_flags;
	u_32_t	fr_logtag;
	u_32_t	fr_collect;
	u_int	fr_arg;
	u_int	fr_loglevel;
	u_int	fr_age[2];
	u_char	fr_v;
	u_char	fr_icode;
	char	fr_group[FR_GROUPLEN];
	char	fr_grhead[FR_GROUPLEN];
	ipftag_t fr_nattag;
	char	fr_ifnames[4][LIFNAMSIZ];
	char	fr_isctag[16];
	frdest_4_t fr_tifs[2];
	frdest_4_t fr_dif;
	u_int	fr_cksum;
} frentry_4_1_0_t;

/* ------------------------------------------------------------------------ */

/*
 * 5.1.0  new release (current)
 * 4.1.32 removed both fin_state and fin_nat, added fin_pktnum
 * 4.1.24 added fin_cksum
 * 4.1.23 added fin_exthdr
 * 4.1.11 added fin_ifname
 * 4.1.4  added fin_hbuf
 */
typedef	struct	fr_info_4_1_32 {
	void	*fin_ifp;		/* interface packet is `on' */
	frip4_t	fin_fi;		/* IP Packet summary */
	union	{
		u_short	fid_16[2];	/* TCP/UDP ports, ICMP code/type */
		u_32_t	fid_32;
	} fin_dat;
	int	fin_out;		/* in or out ? 1 == out, 0 == in */
	int	fin_rev;		/* state only: 1 = reverse */
	u_short	fin_hlen;		/* length of IP header in bytes */
	u_char	ofin_tcpf;		/* TCP header flags (SYN, ACK, etc) */
	u_char	fin_icode;		/* ICMP error to return */
	u_32_t	fin_rule;		/* rule # last matched */
	char	fin_group[FR_GROUPLEN];	/* group number, -1 for none */
	struct	frentry *fin_fr;	/* last matching rule */
	void	*fin_dp;		/* start of data past IP header */
	int	fin_dlen;		/* length of data portion of packet */
	int	fin_plen;
	int	fin_ipoff;		/* # bytes from buffer start to hdr */
	u_short	fin_id;			/* IP packet id field */
	u_short	fin_off;
	int	fin_depth;		/* Group nesting depth */
	int	fin_error;		/* Error code to return */
	int	fin_cksum;		/* -1 bad, 1 good, 0 not done */
	u_int	fin_pktnum;
	void	*fin_nattag;
	void	*fin_exthdr;
	ip_t	*ofin_ip;
	mb_t	**fin_mp;		/* pointer to pointer to mbuf */
	mb_t	*fin_m;			/* pointer to mbuf */
#ifdef	MENTAT
	mb_t	*fin_qfm;		/* pointer to mblk where pkt starts */
	void	*fin_qpi;
	char	fin_ifname[LIFNAMSIZ];
#endif
} fr_info_4_1_32_t;

typedef struct  fr_info_4_1_24 {
	void    *fin_ifp;
	frip4_t fin_fi;
	union   {
		u_short fid_16[2];
		u_32_t  fid_32;
	} fin_dat;
	int     fin_out;
	int     fin_rev;
	u_short fin_hlen;
	u_char  ofin_tcpf;
	u_char  fin_icode;
	u_32_t  fin_rule;
	char    fin_group[FR_GROUPLEN];
	struct  frentry *fin_fr;
	void    *fin_dp;
	int     fin_dlen;
	int     fin_plen;
	int     fin_ipoff;
	u_short fin_id;
	u_short fin_off;
	int     fin_depth;
	int     fin_error;
	int     fin_cksum;
	void	*fin_state;
	void	*fin_nat;
	void    *fin_nattag;
	void    *fin_exthdr;
	ip_t    *ofin_ip;
	mb_t    **fin_mp;
	mb_t    *fin_m;
#ifdef  MENTAT
	mb_t    *fin_qfm;
	void    *fin_qpi;
	char    fin_ifname[LIFNAMSIZ];
#endif
} fr_info_4_1_24_t;

typedef struct  fr_info_4_1_23 {
	void    *fin_ifp;
	frip4_t fin_fi;
	union   {
		u_short fid_16[2];
		u_32_t  fid_32;
	} fin_dat;
	int     fin_out;
	int     fin_rev;
	u_short fin_hlen;
	u_char  ofin_tcpf;
	u_char  fin_icode;
	u_32_t  fin_rule;
	char    fin_group[FR_GROUPLEN];
	struct  frentry *fin_fr;
	void    *fin_dp;
	int     fin_dlen;
	int     fin_plen;
	int     fin_ipoff;
	u_short fin_id;
	u_short fin_off;
	int     fin_depth;
	int     fin_error;
	void	*fin_state;
	void	*fin_nat;
	void    *fin_nattag;
	void    *fin_exthdr;
	ip_t    *ofin_ip;
	mb_t    **fin_mp;
	mb_t    *fin_m;
#ifdef  MENTAT
	mb_t    *fin_qfm;
	void    *fin_qpi;
	char    fin_ifname[LIFNAMSIZ];
#endif
} fr_info_4_1_23_t;

typedef struct  fr_info_4_1_11 {
	void    *fin_ifp;
	frip4_t fin_fi;
	union   {
		u_short fid_16[2];
		u_32_t  fid_32;
	} fin_dat;
	int     fin_out;
	int     fin_rev;
	u_short fin_hlen;
	u_char  ofin_tcpf;
	u_char  fin_icode;
	u_32_t  fin_rule;
	char    fin_group[FR_GROUPLEN];
	struct  frentry *fin_fr;
	void    *fin_dp;
	int     fin_dlen;
	int     fin_plen;
	int     fin_ipoff;
	u_short fin_id;
	u_short fin_off;
	int     fin_depth;
	int     fin_error;
	void	*fin_state;
	void	*fin_nat;
	void    *fin_nattag;
	ip_t    *ofin_ip;
	mb_t    **fin_mp;
	mb_t    *fin_m;
#ifdef  MENTAT
	mb_t    *fin_qfm;
	void    *fin_qpi;
	char    fin_ifname[LIFNAMSIZ];
#endif
} fr_info_4_1_11_t;

/* ------------------------------------------------------------------------ */

typedef	struct	filterstats_4_1 {
	u_long	fr_pass;	/* packets allowed */
	u_long	fr_block;	/* packets denied */
	u_long	fr_nom;		/* packets which don't match any rule */
	u_long	fr_short;	/* packets which are short */
	u_long	fr_ppkl;	/* packets allowed and logged */
	u_long	fr_bpkl;	/* packets denied and logged */
	u_long	fr_npkl;	/* packets unmatched and logged */
	u_long	fr_pkl;		/* packets logged */
	u_long	fr_skip;	/* packets to be logged but buffer full */
	u_long	fr_ret;		/* packets for which a return is sent */
	u_long	fr_acct;	/* packets for which counting was performed */
	u_long	fr_bnfr;	/* bad attempts to allocate fragment state */
	u_long	fr_nfr;		/* new fragment state kept */
	u_long	fr_cfr;		/* add new fragment state but complete pkt */
	u_long	fr_bads;	/* bad attempts to allocate packet state */
	u_long	fr_ads;		/* new packet state kept */
	u_long	fr_chit;	/* cached hit */
	u_long	fr_tcpbad;	/* TCP checksum check failures */
	u_long	fr_pull[2];	/* good and bad pullup attempts */
	u_long	fr_badsrc;	/* source received doesn't match route */
	u_long	fr_badttl;	/* TTL in packet doesn't reach minimum */
	u_long	fr_bad;		/* bad IP packets to the filter */
	u_long	fr_ipv6;	/* IPv6 packets in/out */
	u_long	fr_ppshit;	/* dropped because of pps ceiling */
	u_long	fr_ipud;	/* IP id update failures */
} filterstats_4_1_t;

/*
 * 5.1.0  new release (current)
 * 4.1.33 changed the size of f_locks from IPL_LOGMAX to IPL_LOGSIZE
 */
typedef	struct	friostat_4_1_33	{
	struct	filterstats_4_1	of_st[2];
	struct	frentry	*f_ipf[2][2];
	struct	frentry	*f_acct[2][2];
	struct	frentry	*f_ipf6[2][2];
	struct	frentry	*f_acct6[2][2];
	struct	frentry	*f_auth;
	struct	frgroup	*f_groups[IPL_LOGSIZE][2];
	u_long	f_froute[2];
	u_long	f_ticks;
	int	f_locks[IPL_LOGSIZE];
	size_t	f_kmutex_sz;
	size_t	f_krwlock_sz;
	int	f_defpass;	/* default pass - from fr_pass */
	int	f_active;	/* 1 or 0 - active rule set */
	int	f_running;	/* 1 if running, else 0 */
	int	f_logging;	/* 1 if enabled, else 0 */
	int	f_features;
	char	f_version[32];	/* version string */
} friostat_4_1_33_t;

typedef struct friostat_4_1_0	{
	struct filterstats_4_1 of_st[2];
	struct frentry	*f_ipf[2][2];
	struct frentry	*f_acct[2][2];
	struct frentry	*f_ipf6[2][2];
	struct frentry	*f_acct6[2][2];
	struct frentry	*f_auth;
	struct frgroup	*f_groups[IPL_LOGSIZE][2];
	u_long	f_froute[2];
	u_long	f_ticks;
	int	f_locks[IPL_LOGMAX];
	size_t	f_kmutex_sz;
	size_t	f_krwlock_sz;
	int	f_defpass;
	int	f_active;
	int	f_running;
	int	f_logging;
	int	f_features;
	char	f_version[32];
} friostat_4_1_0_t;

/* ------------------------------------------------------------------------ */

/*
 * 5.1.0  new release (current)
 * 4.1.14 added in_lock
 */
typedef	struct	ipnat_4_1_14	{
	ipfmutex_t	in_lock;
	struct	ipnat	*in_next;		/* NAT rule list next */
	struct	ipnat	*in_rnext;		/* rdr rule hash next */
	struct	ipnat	**in_prnext;		/* prior rdr next ptr */
	struct	ipnat	*in_mnext;		/* map rule hash next */
	struct	ipnat	**in_pmnext;		/* prior map next ptr */
	struct	ipftq	*in_tqehead[2];
	void		*in_ifps[2];
	void		*in_apr;
	char		*in_comment;
	i6addr_t	in_next6;
	u_long		in_space;
	u_long		in_hits;
	u_int		in_use;
	u_int		in_hv;
	int		in_flineno;		/* conf. file line number */
	u_short		in_pnext;
	u_char		in_v;
	u_char		in_xxx;
	/* From here to the end is covered by IPN_CMPSIZ */
	u_32_t		in_flags;
	u_32_t		in_mssclamp;		/* if != 0 clamp MSS to this */
	u_int		in_age[2];
	int		in_redir;		/* see below for values */
	int		in_p;			/* protocol. */
	i6addr_t	in_in[2];
	i6addr_t	in_out[2];
	i6addr_t	in_src[2];
	frtuc4_t	in_tuc;
	u_short		in_port[2];
	u_short		in_ppip;		/* ports per IP. */
	u_short		in_ippip;		/* IP #'s per IP# */
	char		in_ifnames[2][LIFNAMSIZ];
	char		in_plabel[APR_LABELLEN];	/* proxy label. */
	ipftag_t	in_tag;
} ipnat_4_1_14_t;

typedef	struct	ipnat_4_1_0	{
	struct	ipnat	*in_next;
	struct	ipnat	*in_rnext;
	struct	ipnat	**in_prnext;
	struct	ipnat	*in_mnext;
	struct	ipnat	**in_pmnext;
	struct	ipftq	*in_tqehead[2];
	void		*in_ifps[2];
	void		*in_apr;
	char		*in_comment;
	i6addr_t	in_next6;
	u_long		in_space;
	u_long		in_hits;
	u_int		in_use;
	u_int		in_hv;
	int		in_flineno;
	u_short		in_pnext;
	u_char		in_v;
	u_char		in_xxx;
	u_32_t		in_flags;
	u_32_t		in_mssclamp;
	u_int		in_age[2];
	int		in_redir;
	int		in_p;
	i6addr_t	in_in[2];
	i6addr_t	in_out[2];
	i6addr_t	in_src[2];
	frtuc4_t	in_tuc;
	u_short		in_port[2];
	u_short		in_ppip;
	u_short		in_ippip;
	char		in_ifnames[2][LIFNAMSIZ];
	char		in_plabel[APR_LABELLEN];
	ipftag_t	in_tag;
} ipnat_4_1_0_t;

/* ------------------------------------------------------------------------ */

typedef	struct	natlookup_4_1_1 {
	struct	in_addr	onl_inip;
	struct	in_addr	onl_outip;
	struct	in_addr	onl_realip;
	int	nl_flags;
	u_short	nl_inport;
	u_short	nl_outport;
	u_short	nl_realport;
} natlookup_4_1_1_t;

/* ------------------------------------------------------------------------ */

/*
 * 4.1.25 added nat_seqnext (current)
 * 4.1.14 added nat_redir
 * 4.1.3  moved nat_rev
 * 4.1.2  added nat_rev
 */
typedef	struct	nat_4_1_25	{
	ipfmutex_t	nat_lock;
	struct	nat_4_1_25	*nat_next;
	struct	nat_4_1_25	**nat_pnext;
	struct	nat_4_1_25	*nat_hnext[2];
	struct	nat_4_1_25	**nat_phnext[2];
	struct	hostmap	*nat_hm;
	void		*nat_data;
	struct	nat_4_1_25	**nat_me;
	struct	ipstate	*nat_state;
	struct	ap_session	*nat_aps;
	frentry_t	*nat_fr;
	struct	ipnat_4_1_14	*nat_ptr;
	void		*nat_ifps[2];
	void		*nat_sync;
	ipftqent_t	nat_tqe;
	u_32_t		nat_flags;
	u_32_t		nat_sumd[2];
	u_32_t		nat_ipsumd;
	u_32_t		nat_mssclamp;
	i6addr_t	nat_inip6;
	i6addr_t	nat_outip6;
	i6addr_t	nat_oip6;
	U_QUAD_T	nat_pkts[2];
	U_QUAD_T	nat_bytes[2];
	union	{
		udpinfo_t	nat_unu;
		tcpinfo4_t	nat_unt;
		icmpinfo_t	nat_uni;
		greinfo_t	nat_ugre;
	} nat_un;
	u_short		nat_oport;
	u_short		nat_use;
	u_char		nat_p;
	int		nat_dir;
	int		nat_ref;
	int		nat_hv[2];
	char		nat_ifnames[2][LIFNAMSIZ];
	int		nat_rev;
	int		nat_redir;
	u_32_t		nat_seqnext[2];
} nat_4_1_25_t;

typedef	struct	nat_4_1_14	{
	ipfmutex_t	nat_lock;
	struct	nat	*nat_next;
	struct	nat	**nat_pnext;
	struct	nat	*nat_hnext[2];
	struct	nat	**nat_phnext[2];
	struct	hostmap	*nat_hm;
	void		*nat_data;
	struct	nat	**nat_me;
	struct	ipstate	*nat_state;
	struct	ap_session	*nat_aps;
	frentry_t	*nat_fr;
	struct	ipnat	*nat_ptr;
	void		*nat_ifps[2];
	void		*nat_sync;
	ipftqent_t	nat_tqe;
	u_32_t		nat_flags;
	u_32_t		nat_sumd[2];
	u_32_t		nat_ipsumd;
	u_32_t		nat_mssclamp;
	i6addr_t	nat_inip6;
	i6addr_t	nat_outip6;
	i6addr_t	nat_oip6;
	U_QUAD_T	nat_pkts[2];
	U_QUAD_T	nat_bytes[2];
	union	{
		udpinfo_t	nat_unu;
		tcpinfo4_t	nat_unt;
		icmpinfo_t	nat_uni;
		greinfo_t	nat_ugre;
	} nat_un;
	u_short		nat_oport;
	u_short		nat_use;
	u_char		nat_p;
	int		nat_dir;
	int		nat_ref;
	int		nat_hv[2];
	char		nat_ifnames[2][LIFNAMSIZ];
	int		nat_rev;
	int		nat_redir;
} nat_4_1_14_t;

typedef	struct	nat_4_1_3	{
	ipfmutex_t	nat_lock;
	struct	nat	*nat_next;
	struct	nat	**nat_pnext;
	struct	nat	*nat_hnext[2];
	struct	nat	**nat_phnext[2];
	struct	hostmap	*nat_hm;
	void		*nat_data;
	struct	nat	**nat_me;
	struct	ipstate	*nat_state;
	struct	ap_session	*nat_aps;
	frentry_t	*nat_fr;
	struct	ipnat	*nat_ptr;
	void		*nat_ifps[2];
	void		*nat_sync;
	ipftqent_t	nat_tqe;
	u_32_t		nat_flags;
	u_32_t		nat_sumd[2];
	u_32_t		nat_ipsumd;
	u_32_t		nat_mssclamp;
	i6addr_t	nat_inip6;
	i6addr_t	nat_outip6;
	i6addr_t	nat_oip6;
	U_QUAD_T	nat_pkts[2];
	U_QUAD_T	nat_bytes[2];
	union	{
		udpinfo_t	nat_unu;
		tcpinfo4_t	nat_unt;
		icmpinfo_t	nat_uni;
		greinfo_t	nat_ugre;
	} nat_un;
	u_short		nat_oport;
	u_short		nat_use;
	u_char		nat_p;
	int		nat_dir;
	int		nat_ref;
	int		nat_hv[2];
	char		nat_ifnames[2][LIFNAMSIZ];
	int		nat_rev;
} nat_4_1_3_t;



typedef struct  nat_save_4_1_34    {
	void			*ipn_next;
	struct	nat_4_1_25	ipn_nat;
	struct	ipnat_4_1_14	ipn_ipnat;
	struct	frentry_4_1_34 	ipn_fr;
	int			ipn_dsize;
	char			ipn_data[4];
} nat_save_4_1_34_t;

typedef	struct	nat_save_4_1_16	{
	void		*ipn_next;
	nat_4_1_14_t	ipn_nat;
	ipnat_t		ipn_ipnat;
	frentry_4_1_16_t	ipn_fr;
	int		ipn_dsize;
	char		ipn_data[4];
} nat_save_4_1_16_t;

typedef	struct	nat_save_4_1_14	{
	void		*ipn_next;
	nat_4_1_14_t	ipn_nat;
	ipnat_t		ipn_ipnat;
	frentry_4_1_0_t	ipn_fr;
	int		ipn_dsize;
	char		ipn_data[4];
} nat_save_4_1_14_t;

typedef	struct	nat_save_4_1_3	{
	void		*ipn_next;
	nat_4_1_3_t	ipn_nat;
	ipnat_4_1_0_t	ipn_ipnat;
	frentry_4_1_0_t	ipn_fr;
	int		ipn_dsize;
	char		ipn_data[4];
} nat_save_4_1_3_t;

/* ------------------------------------------------------------------------ */

/*
 * 5.1.0  new release (current)
 * 4.1.32 added ns_uncreate
 * 4.1.27 added ns_orphans
 * 4.1.16 added ns_ticks
 */
typedef	struct	natstat_4_1_32	{
	u_long	ns_mapped[2];
	u_long	ns_rules;
	u_long	ns_added;
	u_long	ns_expire;
	u_long	ns_inuse;
	u_long	ns_logged;
	u_long	ns_logfail;
	u_long	ns_memfail;
	u_long	ns_badnat;
	u_long	ns_addtrpnt;
	nat_t	**ns_table[2];
	hostmap_t **ns_maptable;
	ipnat_t	*ns_list;
	void	*ns_apslist;
	u_int	ns_wilds;
	u_int	ns_nattab_sz;
	u_int	ns_nattab_max;
	u_int	ns_rultab_sz;
	u_int	ns_rdrtab_sz;
	u_int	ns_trpntab_sz;
	u_int	ns_hostmap_sz;
	nat_t	*ns_instances;
	hostmap_t *ns_maplist;
	u_long	*ns_bucketlen[2];
	u_long	ns_ticks;
	u_int	ns_orphans;
	u_long	ns_uncreate[2][2];
} natstat_4_1_32_t;

typedef struct  natstat_4_1_27 {
	u_long	ns_mapped[2];
	u_long	ns_rules;
	u_long	ns_added;
	u_long	ns_expire;
	u_long	ns_inuse;
	u_long	ns_logged;
	u_long	ns_logfail;
	u_long	ns_memfail;
	u_long	ns_badnat;
	u_long	ns_addtrpnt;
	nat_t	**ns_table[2];
	hostmap_t **ns_maptable;
	ipnat_t *ns_list;
	void    *ns_apslist;
	u_int   ns_wilds;
	u_int   ns_nattab_sz;
	u_int   ns_nattab_max;
	u_int   ns_rultab_sz;
	u_int   ns_rdrtab_sz;
	u_int   ns_trpntab_sz;
	u_int   ns_hostmap_sz;
	nat_t   *ns_instances;
	hostmap_t *ns_maplist;
	u_long  *ns_bucketlen[2];
	u_long  ns_ticks;
	u_int   ns_orphans;
} natstat_4_1_27_t;

typedef struct  natstat_4_1_16 {
	u_long	ns_mapped[2];
	u_long	ns_rules;
	u_long	ns_added;
	u_long	ns_expire;
	u_long	ns_inuse;
	u_long	ns_logged;
	u_long	ns_logfail;
	u_long	ns_memfail;
	u_long	ns_badnat;
	u_long	ns_addtrpnt;
	nat_t	**ns_table[2];
	hostmap_t **ns_maptable;
	ipnat_t *ns_list;
	void    *ns_apslist;
	u_int   ns_wilds;
	u_int   ns_nattab_sz;
	u_int   ns_nattab_max;
	u_int   ns_rultab_sz;
	u_int   ns_rdrtab_sz;
	u_int   ns_trpntab_sz;
	u_int   ns_hostmap_sz;
	nat_t   *ns_instances;
	hostmap_t *ns_maplist;
	u_long  *ns_bucketlen[2];
	u_long  ns_ticks;
} natstat_4_1_16_t;

typedef struct  natstat_4_1_0 {
	u_long	ns_mapped[2];
	u_long	ns_rules;
	u_long	ns_added;
	u_long	ns_expire;
	u_long	ns_inuse;
	u_long	ns_logged;
	u_long	ns_logfail;
	u_long	ns_memfail;
	u_long	ns_badnat;
	u_long	ns_addtrpnt;
	nat_t	**ns_table[2];
	hostmap_t **ns_maptable;
	ipnat_t *ns_list;
	void    *ns_apslist;
	u_int   ns_wilds;
	u_int   ns_nattab_sz;
	u_int   ns_nattab_max;
	u_int   ns_rultab_sz;
	u_int   ns_rdrtab_sz;
	u_int   ns_trpntab_sz;
	u_int   ns_hostmap_sz;
	nat_t   *ns_instances;
	hostmap_t *ns_maplist;
	u_long  *ns_bucketlen[2];
} natstat_4_1_0_t;

/* ------------------------------------------------------------------------ */

/*
 * 5.1.0  new release (current)
 * 4.1.32 fra_info:removed both fin_state & fin_nat, added fin_pktnum
 * 4.1.29 added fra_flx
 * 4.1.24 fra_info:added fin_cksum
 * 4.1.23 fra_info:added fin_exthdr
 * 4.1.11 fra_info:added fin_ifname
 * 4.1.4  fra_info:added fin_hbuf
 */

typedef struct  frauth_4_1_32 {
	int	fra_age;
	int	fra_len;
	int	fra_index;
	u_32_t	fra_pass;
	fr_info_4_1_32_t	fra_info;
	char	*fra_buf;
	u_32_t	fra_flx;
#ifdef	MENTAT
	queue_t	*fra_q;
	mb_t	*fra_m;
#endif
} frauth_4_1_32_t;

typedef struct  frauth_4_1_29 {
	int	fra_age;
	int	fra_len;
	int	fra_index;
	u_32_t	fra_pass;
	fr_info_4_1_24_t	fra_info;
	char	*fra_buf;
	u_32_t	fra_flx;
#ifdef	MENTAT
	queue_t	*fra_q;
	mb_t	*fra_m;
#endif
} frauth_4_1_29_t;

typedef struct  frauth_4_1_24 {
	int	fra_age;
	int	fra_len;
	int	fra_index;
	u_32_t	fra_pass;
	fr_info_4_1_24_t	fra_info;
	char	*fra_buf;
#ifdef	MENTAT
	queue_t	*fra_q;
	mb_t	*fra_m;
#endif
} frauth_4_1_24_t;

typedef struct  frauth_4_1_23 {
	int	fra_age;
	int	fra_len;
	int	fra_index;
	u_32_t	fra_pass;
	fr_info_4_1_23_t	fra_info;
	char	*fra_buf;
#ifdef	MENTAT
	queue_t	*fra_q;
	mb_t	*fra_m;
#endif
} frauth_4_1_23_t;

typedef struct  frauth_4_1_11 {
	int	fra_age;
	int	fra_len;
	int	fra_index;
	u_32_t	fra_pass;
	fr_info_4_1_11_t	fra_info;
	char	*fra_buf;
#ifdef	MENTAT
	queue_t	*fra_q;
	mb_t	*fra_m;
#endif
} frauth_4_1_11_t;

/* ------------------------------------------------------------------------ */

/*
 * 5.1.0  new release (current)
 * 4.1.16 removed is_nat
 */
typedef struct ipstate_4_1_16 {
	ipfmutex_t	is_lock;
	struct	ipstate	*is_next;
	struct	ipstate	**is_pnext;
	struct	ipstate	*is_hnext;
	struct	ipstate	**is_phnext;
	struct	ipstate	**is_me;
	void		*is_ifp[4];
	void		*is_sync;
	frentry_t	*is_rule;
	struct	ipftq	*is_tqehead[2];
	struct	ipscan	*is_isc;
	U_QUAD_T	is_pkts[4];
	U_QUAD_T	is_bytes[4];
	U_QUAD_T	is_icmppkts[4];
	struct	ipftqent is_sti;
	u_int	is_frage[2];
	int	is_ref;			/* reference count */
	int	is_isninc[2];
	u_short	is_sumd[2];
	i6addr_t	is_src;
	i6addr_t	is_dst;
	u_int	is_pass;
	u_char	is_p;			/* Protocol */
	u_char	is_v;
	u_32_t	is_hv;
	u_32_t	is_tag;
	u_32_t	is_opt[2];		/* packet options set */
	u_32_t	is_optmsk[2];		/*    "      "    mask */
	u_short	is_sec;			/* security options set */
	u_short	is_secmsk;		/*    "        "    mask */
	u_short	is_auth;		/* authentication options set */
	u_short	is_authmsk;		/*    "              "    mask */
	union {
		icmpinfo_t	is_ics;
		tcpinfo4_t	is_ts;
		udpinfo_t	is_us;
		greinfo_t	is_ug;
	} is_ps;
	u_32_t	is_flags;
	int	is_flx[2][2];
	u_32_t	is_rulen;		/* rule number when created */
	u_32_t	is_s0[2];
	u_short	is_smsk[2];
	char	is_group[FR_GROUPLEN];
	char	is_sbuf[2][16];
	char	is_ifname[4][LIFNAMSIZ];
} ipstate_4_1_16_t;

typedef struct ipstate_4_1_0 {
	ipfmutex_t	is_lock;
	struct	ipstate	*is_next;
	struct	ipstate	**is_pnext;
	struct	ipstate	*is_hnext;
	struct	ipstate	**is_phnext;
	struct	ipstate	**is_me;
	void		*is_ifp[4];
	void		*is_sync;
	void		*is_nat[2];
	frentry_t	*is_rule;
	struct	ipftq	*is_tqehead[2];
	struct	ipscan	*is_isc;
	U_QUAD_T	is_pkts[4];
	U_QUAD_T	is_bytes[4];
	U_QUAD_T	is_icmppkts[4];
	struct	ipftqent is_sti;
	u_int	is_frage[2];
	int	is_ref;
	int	is_isninc[2];
	u_short	is_sumd[2];
	i6addr_t	is_src;
	i6addr_t	is_dst;
	u_int	is_pass;
	u_char	is_p;
	u_char	is_v;
	u_32_t	is_hv;
	u_32_t	is_tag;
	u_32_t	is_opt[2];
	u_32_t	is_optmsk[2];
	u_short	is_sec;
	u_short	is_secmsk;
	u_short	is_auth;
	u_short	is_authmsk;
	union {
		icmpinfo_t	is_ics;
		tcpinfo4_t	is_ts;
		udpinfo_t	is_us;
		greinfo_t	is_ug;
	} is_ps;
	u_32_t	is_flags;
	int	is_flx[2][2];
	u_32_t	is_rulen;
	u_32_t	is_s0[2];
	u_short	is_smsk[2];
	char	is_group[FR_GROUPLEN];
	char	is_sbuf[2][16];
	char	is_ifname[4][LIFNAMSIZ];
} ipstate_4_1_0_t;

typedef	struct	ipstate_save_4_1_34	{
	void	*ips_next;
	struct	ipstate_4_1_16	ips_is;
	struct	frentry_4_1_34	ips_fr;
} ipstate_save_4_1_34_t;

typedef	struct	ipstate_save_4_1_16	{
	void		*ips_next;
	ipstate_4_1_0_t	ips_is;
	frentry_4_1_16_t	ips_fr;
} ipstate_save_4_1_16_t;

typedef	struct	ipstate_save_4_1_0	{
	void		*ips_next;
	ipstate_4_1_0_t	ips_is;
	frentry_4_1_0_t	ips_fr;
} ipstate_save_4_1_0_t;

/* ------------------------------------------------------------------------ */

/*
 * 5.1.0  new release (current)
 * 4.1.21 added iss_tcptab
 */
typedef	struct	ips_stat_4_1_21 {
	u_long	iss_hits;
	u_long	iss_miss;
	u_long	iss_max;
	u_long	iss_maxref;
	u_long	iss_tcp;
	u_long	iss_udp;
	u_long	iss_icmp;
	u_long	iss_nomem;
	u_long	iss_expire;
	u_long	iss_fin;
	u_long	iss_active;
	u_long	iss_logged;
	u_long	iss_logfail;
	u_long	iss_inuse;
	u_long	iss_wild;
	u_long	iss_killed;
	u_long	iss_ticks;
	u_long	iss_bucketfull;
	int	iss_statesize;
	int	iss_statemax;
	ipstate_t **iss_table;
	ipstate_t *iss_list;
	u_long	*iss_bucketlen;
	ipftq_t	*iss_tcptab;
} ips_stat_4_1_21_t;

typedef	struct	ips_stat_4_1_0 {
	u_long	iss_hits;
	u_long	iss_miss;
	u_long	iss_max;
	u_long	iss_maxref;
	u_long	iss_tcp;
	u_long	iss_udp;
	u_long	iss_icmp;
	u_long	iss_nomem;
	u_long	iss_expire;
	u_long	iss_fin;
	u_long	iss_active;
	u_long	iss_logged;
	u_long	iss_logfail;
	u_long	iss_inuse;
	u_long	iss_wild;
	u_long	iss_killed;
	u_long	iss_ticks;
	u_long	iss_bucketfull;
	int	iss_statesize;
	int	iss_statemax;
	ipstate_t **iss_table;
	ipstate_t *iss_list;
	u_long	*iss_bucketlen;
} ips_stat_4_1_0_t;

/* ------------------------------------------------------------------------ */

typedef	struct	ipfrstat_4_1_1 {
	u_long	ifs_exists;	/* add & already exists */
	u_long	ifs_nomem;
	u_long	ifs_new;
	u_long	ifs_hits;
	u_long	ifs_expire;
	u_long	ifs_inuse;
	u_long	ifs_retrans0;
	u_long	ifs_short;
	struct	ipfr	**ifs_table;
	struct	ipfr	**ifs_nattab;
} ipfrstat_4_1_1_t;

/* ------------------------------------------------------------------------ */
static int ipf_addfrstr __P((char *, int, char *, int));
static void ipf_v4iptov5 __P((frip4_t *, fr_ip_t *));
static void ipf_v5iptov4 __P((fr_ip_t *, frip4_t *));
static void ipfv4tuctov5 __P((frtuc4_t *, frtuc_t *));
static void ipfv5tuctov4 __P((frtuc_t *, frtuc4_t *));
static int ipf_v4fripftov5 __P((fripf4_t *, char *));
static void ipf_v5fripftov4 __P((fripf_t *, fripf4_t *));
static int fr_frflags4to5 __P((u_32_t));
static int fr_frflags5to4 __P((u_32_t));

static void friostat_current_to_4_1_0 __P((void *, friostat_4_1_0_t *, int));
static void friostat_current_to_4_1_33 __P((void *, friostat_4_1_33_t *, int));
static void ipstate_current_to_4_1_0 __P((void *, ipstate_4_1_0_t *));
static void ipstate_current_to_4_1_16 __P((void *, ipstate_4_1_16_t *));
static void ipnat_current_to_4_1_0 __P((void *, ipnat_4_1_0_t *));
static void ipnat_current_to_4_1_14 __P((void *, ipnat_4_1_14_t *));
static void frauth_current_to_4_1_11 __P((void *, frauth_4_1_11_t *));
static void frauth_current_to_4_1_23 __P((void *, frauth_4_1_23_t *));
static void frauth_current_to_4_1_24 __P((void *, frauth_4_1_24_t *));
static void frauth_current_to_4_1_29 __P((void *, frauth_4_1_29_t *));
static void frentry_current_to_4_1_0 __P((void *, frentry_4_1_0_t *));
static void frentry_current_to_4_1_16 __P((void *, frentry_4_1_16_t *));
static void frentry_current_to_4_1_34 __P((void *, frentry_4_1_34_t *));
static void fr_info_current_to_4_1_11 __P((void *, fr_info_4_1_11_t *));
static void fr_info_current_to_4_1_23 __P((void *, fr_info_4_1_23_t *));
static void fr_info_current_to_4_1_24 __P((void *, fr_info_4_1_24_t *));
static void nat_save_current_to_4_1_3 __P((void *, nat_save_4_1_3_t *));
static void nat_save_current_to_4_1_14 __P((void *, nat_save_4_1_14_t *));
static void nat_save_current_to_4_1_16 __P((void *, nat_save_4_1_16_t *));
static void ipstate_save_current_to_4_1_0 __P((void *, ipstate_save_4_1_0_t *));
static void ipstate_save_current_to_4_1_16 __P((void *, ipstate_save_4_1_16_t *));
static void ips_stat_current_to_4_1_0 __P((void *, ips_stat_4_1_0_t *));
static void ips_stat_current_to_4_1_21 __P((void *, ips_stat_4_1_21_t *));
static void natstat_current_to_4_1_0 __P((void *, natstat_4_1_0_t *));
static void natstat_current_to_4_1_16 __P((void *, natstat_4_1_16_t *));
static void natstat_current_to_4_1_27 __P((void *, natstat_4_1_27_t *));
static void natstat_current_to_4_1_32 __P((void *, natstat_4_1_32_t *));
static void nat_current_to_4_1_3 __P((void *, nat_4_1_3_t *));
static void nat_current_to_4_1_14 __P((void *, nat_4_1_14_t *));
static void nat_current_to_4_1_25 __P((void *, nat_4_1_25_t *));

static void friostat_4_1_0_to_current __P((friostat_4_1_0_t *, void *));
static void friostat_4_1_33_to_current __P((friostat_4_1_33_t *, void *));
static void ipnat_4_1_0_to_current __P((ipnat_4_1_0_t *, void *, int));
static void ipnat_4_1_14_to_current __P((ipnat_4_1_14_t *, void *, int));
static void frauth_4_1_11_to_current __P((frauth_4_1_11_t *, void *));
static void frauth_4_1_23_to_current __P((frauth_4_1_23_t *, void *));
static void frauth_4_1_24_to_current __P((frauth_4_1_24_t *, void *));
static void frauth_4_1_29_to_current __P((frauth_4_1_29_t *, void *));
static void frauth_4_1_32_to_current __P((frauth_4_1_32_t *, void *));
static void frentry_4_1_0_to_current __P((ipf_main_softc_t *, frentry_4_1_0_t *, void *, int));
static void frentry_4_1_16_to_current __P((ipf_main_softc_t *, frentry_4_1_16_t *, void *, int));
static void frentry_4_1_34_to_current __P((ipf_main_softc_t *, frentry_4_1_34_t *, void *, int));
static void fr_info_4_1_11_to_current __P((fr_info_4_1_11_t *, void *));
static void fr_info_4_1_23_to_current __P((fr_info_4_1_23_t *, void *));
static void fr_info_4_1_24_to_current __P((fr_info_4_1_24_t *, void *));
static void fr_info_4_1_32_to_current __P((fr_info_4_1_32_t *, void *));
static void nat_save_4_1_3_to_current __P((ipf_main_softc_t *, nat_save_4_1_3_t *, void *));
static void nat_save_4_1_14_to_current __P((ipf_main_softc_t *, nat_save_4_1_14_t *, void *));
static void nat_save_4_1_16_to_current __P((ipf_main_softc_t *, nat_save_4_1_16_t *, void *));

/* ------------------------------------------------------------------------ */
/* In this section is a series of short routines that deal with translating */
/* the smaller data structures used above as their internal changes make    */
/* them inappropriate for simple assignment.                                */
/* ------------------------------------------------------------------------ */


static int
ipf_addfrstr(char *names, int namelen, char *str, int maxlen)
{
	char *t;
	int i;

	for (i = maxlen, t = str; (*t != '\0') && (i > 0); i--) {
		names[namelen++] = *t++;
	}
	names[namelen++] = '\0';
	return namelen;
}


static void
ipf_v4iptov5(v4, v5)
	frip4_t *v4;
	fr_ip_t *v5;
{
	v5->fi_v = v4->fi_v;
	v5->fi_p = v4->fi_p;
	v5->fi_xx = v4->fi_xx;
	v5->fi_tos = v4->fi_tos;
	v5->fi_ttl = v4->fi_ttl;
	v5->fi_p = v4->fi_p;
	v5->fi_optmsk = v4->fi_optmsk;
	v5->fi_src = v4->fi_src;
	v5->fi_dst = v4->fi_dst;
	v5->fi_secmsk = v4->ofi_secmsk;
	v5->fi_auth = v4->ofi_auth;
	v5->fi_flx = v4->fi_flx;
	v5->fi_tcpmsk = v4->fi_tcpmsk;
}

static void
ipf_v5iptov4(v5, v4)
	fr_ip_t *v5;
	frip4_t *v4;
{
	v4->fi_v = v5->fi_v;
	v4->fi_p = v5->fi_p;
	v4->fi_xx = v5->fi_xx;
	v4->fi_tos = v5->fi_tos;
	v4->fi_ttl = v5->fi_ttl;
	v4->fi_p = v5->fi_p;
	v4->fi_optmsk = v5->fi_optmsk;
	v4->fi_src = v5->fi_src;
	v4->fi_dst = v5->fi_dst;
	v4->ofi_secmsk = v5->fi_secmsk;
	v4->ofi_auth = v5->fi_auth;
	v4->fi_flx = v5->fi_flx;
	v4->fi_tcpmsk = v5->fi_tcpmsk;
}


static void
ipfv4tuctov5(v4, v5)
	frtuc4_t *v4;
	frtuc_t *v5;
{
	v5->ftu_src.frp_cmp = v4->ftu_src.frp_cmp;
	v5->ftu_src.frp_port = v4->ftu_src.frp_port;
	v5->ftu_src.frp_top = v4->ftu_src.frp_top;
	v5->ftu_dst.frp_cmp = v4->ftu_dst.frp_cmp;
	v5->ftu_dst.frp_port = v4->ftu_dst.frp_port;
	v5->ftu_dst.frp_top = v4->ftu_dst.frp_top;
}


static void
ipfv5tuctov4(v5, v4)
	frtuc_t *v5;
	frtuc4_t *v4;
{
	v4->ftu_src.frp_cmp = v5->ftu_src.frp_cmp;
	v4->ftu_src.frp_port = v5->ftu_src.frp_port;
	v4->ftu_src.frp_top = v5->ftu_src.frp_top;
	v4->ftu_dst.frp_cmp = v5->ftu_dst.frp_cmp;
	v4->ftu_dst.frp_port = v5->ftu_dst.frp_port;
	v4->ftu_dst.frp_top = v5->ftu_dst.frp_top;
}


static int
ipf_v4fripftov5(frp4, dst)
	fripf4_t *frp4;
	char *dst;
{
	fripf_t *frp;

	frp = (fripf_t *)dst;

	ipf_v4iptov5(&frp4->fri_ip, &frp->fri_ip);
	ipf_v4iptov5(&frp4->fri_mip, &frp->fri_mip);
	frp->fri_icmpm = frp4->fri_icmpm;
	frp->fri_icmp = frp4->fri_icmp;
	frp->fri_tuc.ftu_tcpfm = frp4->fri_tuc.ftu_tcpfm;
	frp->fri_tuc.ftu_tcpf = frp4->fri_tuc.ftu_tcpf;
	ipfv4tuctov5(&frp4->fri_tuc, &frp->fri_tuc);
	frp->fri_satype = frp4->fri_satype;
	frp->fri_datype = frp4->fri_datype;
	frp->fri_sifpidx = frp4->fri_sifpidx;
	frp->fri_difpidx = frp4->fri_difpidx;
	return 0;
}


static void
ipf_v5fripftov4(frp, frp4)
	fripf_t *frp;
	fripf4_t *frp4;
{

	ipf_v5iptov4(&frp->fri_ip, &frp4->fri_ip);
	ipf_v5iptov4(&frp->fri_mip, &frp4->fri_mip);
	frp4->fri_icmpm = frp->fri_icmpm;
	frp4->fri_icmp = frp->fri_icmp;
	frp4->fri_tuc.ftu_tcpfm = frp->fri_tuc.ftu_tcpfm;
	frp4->fri_tuc.ftu_tcpf = frp->fri_tuc.ftu_tcpf;
	ipfv5tuctov4(&frp->fri_tuc, &frp4->fri_tuc);
	frp4->fri_satype = frp->fri_satype;
	frp4->fri_datype = frp->fri_datype;
	frp4->fri_sifpidx = frp->fri_sifpidx;
	frp4->fri_difpidx = frp->fri_difpidx;
}


/* ------------------------------------------------------------------------ */
/* ipf_in_compat is the first of two service routines. It is responsible for*/
/* converting data structures from user space into what's required by the   */
/* kernel module.                                                           */
/* ------------------------------------------------------------------------ */
int
ipf_in_compat(softc, obj, ptr, size)
	ipf_main_softc_t *softc;
	ipfobj_t *obj;
	void *ptr;
	int size;
{
	int error;
	int sz;

	IPFERROR(140000);
	error = EINVAL;

	switch (obj->ipfo_type)
	{
	default :
		break;

	case IPFOBJ_FRENTRY :
		if (obj->ipfo_rev >= 4013400) {
			frentry_4_1_34_t *old;

			KMALLOC(old, frentry_4_1_34_t *);
			if (old == NULL) {
				IPFERROR(140001);
				error = ENOMEM;
				break;
			}
			error = COPYIN(obj->ipfo_ptr, old, sizeof(*old));
			if (error == 0) {
				if (old->fr_type != FR_T_NONE &&
				    old->fr_type != FR_T_IPF) {
					IPFERROR(140002);
					error = EINVAL;
					KFREE(old);
					break;
				}
				frentry_4_1_34_to_current(softc, old,
							  ptr, size);
			} else {
				IPFERROR(140003);
			}
			KFREE(old);
		} else if (obj->ipfo_rev >= 4011600) {
			frentry_4_1_16_t *old;

			KMALLOC(old, frentry_4_1_16_t *);
			if (old == NULL) {
				IPFERROR(140004);
				error = ENOMEM;
				break;
			}
			error = COPYIN(obj->ipfo_ptr, old, sizeof(*old));
			if (error == 0) {
				if (old->fr_type != FR_T_NONE &&
				    old->fr_type != FR_T_IPF) {
					IPFERROR(140005);
					error = EINVAL;
					KFREE(old);
					break;
				}
				frentry_4_1_16_to_current(softc, old,
							  ptr, size);
			} else {
				IPFERROR(140006);
			}
			KFREE(old);
		} else {
			frentry_4_1_0_t *old;

			KMALLOC(old, frentry_4_1_0_t *);
			if (old == NULL) {
				IPFERROR(140007);
				error = ENOMEM;
				break;
			}
			error = COPYIN(obj->ipfo_ptr, old, sizeof(*old));
			if (error == 0) {
				if (old->fr_type != FR_T_NONE &&
				    old->fr_type != FR_T_IPF) {
					IPFERROR(140008);
					error = EINVAL;
					KFREE(old);
					break;
				}
				frentry_4_1_0_to_current(softc, old, ptr, size);
			} else {
				IPFERROR(140009);
			}
			KFREE(old);
		}
		break;

	case IPFOBJ_IPFSTAT :
		if (obj->ipfo_rev >= 4013300) {
			friostat_4_1_33_t *old;

			KMALLOC(old, friostat_4_1_33_t *);
			if (old == NULL) {
				IPFERROR(140010);
				error = ENOMEM;
				break;
			}
			error = COPYIN(obj->ipfo_ptr, old, sizeof(*old));
			if (error == 0) {
				friostat_4_1_33_to_current(old, ptr);
			} else {
				IPFERROR(140011);
			}
		} else {
			friostat_4_1_0_t *old;

			KMALLOC(old, friostat_4_1_0_t *);
			if (old == NULL) {
				IPFERROR(140012);
				error = ENOMEM;
				break;
			}
			error = COPYIN(obj->ipfo_ptr, old, sizeof(*old));
			if (error == 0) {
				friostat_4_1_0_to_current(old, ptr);
			} else {
				IPFERROR(140013);
			}
		}
		break;

	case IPFOBJ_IPFINFO :	/* unused */
		break;

	case IPFOBJ_IPNAT :
		if (obj->ipfo_rev >= 4011400) {
			ipnat_4_1_14_t *old;

			KMALLOC(old, ipnat_4_1_14_t *);
			if (old == NULL) {
				IPFERROR(140014);
				error = ENOMEM;
				break;
			}
			error = COPYIN(obj->ipfo_ptr, old, sizeof(*old));
			if (error == 0) {
				ipnat_4_1_14_to_current(old, ptr, size);
			} else {
				IPFERROR(140015);
			}
			KFREE(old);
		} else {
			ipnat_4_1_0_t *old;

			KMALLOC(old, ipnat_4_1_0_t *);
			if (old == NULL) {
				IPFERROR(140016);
				error = ENOMEM;
				break;
			}
			error = COPYIN(obj->ipfo_ptr, old, sizeof(*old));
			if (error == 0) {
				ipnat_4_1_0_to_current(old, ptr, size);
			} else {
				IPFERROR(140017);
			}
			KFREE(old);
		}
		break;

	case IPFOBJ_NATSTAT :
		/*
		 * Statistics are not copied in.
		 */
		break;

	case IPFOBJ_NATSAVE :
		if (obj->ipfo_rev >= 4011600) {
			nat_save_4_1_16_t *old16;

			KMALLOC(old16, nat_save_4_1_16_t *);
			if (old16 == NULL) {
				IPFERROR(140018);
				error = ENOMEM;
				break;
			}
			error = COPYIN(obj->ipfo_ptr, old16, sizeof(*old16));
			if (error == 0) {
				nat_save_4_1_16_to_current(softc, old16, ptr);
			} else {
				IPFERROR(140019);
			}
			KFREE(old16);
		} else if (obj->ipfo_rev >= 4011400) {
			nat_save_4_1_14_t *old14;

			KMALLOC(old14, nat_save_4_1_14_t *);
			if (old14 == NULL) {
				IPFERROR(140020);
				error = ENOMEM;
				break;
			}
			error = COPYIN(obj->ipfo_ptr, old14, sizeof(*old14));
			if (error == 0) {
				nat_save_4_1_14_to_current(softc, old14, ptr);
			} else {
				IPFERROR(140021);
			}
			KFREE(old14);
		} else if (obj->ipfo_rev >= 4010300) {
			nat_save_4_1_3_t *old3;

			KMALLOC(old3, nat_save_4_1_3_t *);
			if (old3 == NULL) {
				IPFERROR(140022);
				error = ENOMEM;
				break;
			}
			error = COPYIN(obj->ipfo_ptr, old3, sizeof(*old3));
			if (error == 0) {
				nat_save_4_1_3_to_current(softc, old3, ptr);
			} else {
				IPFERROR(140023);
			}
			KFREE(old3);
		}
		break;

	case IPFOBJ_STATESAVE :
		if (obj->ipfo_rev >= 4013400) {
			ipstate_save_4_1_34_t *old;

			KMALLOC(old, ipstate_save_4_1_34_t *);
			if (old == NULL) {
				IPFERROR(140024);
				error = ENOMEM;
				break;
			}
			error = COPYIN(obj->ipfo_ptr, old, sizeof(*old));
			if (error != 0) {
				IPFERROR(140025);
			}
			KFREE(old);
		} else if (obj->ipfo_rev >= 4011600) {
			ipstate_save_4_1_16_t *old;

			KMALLOC(old, ipstate_save_4_1_16_t *);
			if (old == NULL) {
				IPFERROR(140026);
				error = ENOMEM;
				break;
			}
			error = COPYIN(obj->ipfo_ptr, old, sizeof(*old));
			if (error != 0) {
				IPFERROR(140027);
			}
			KFREE(old);
		} else {
			ipstate_save_4_1_0_t *old;

			KMALLOC(old, ipstate_save_4_1_0_t *);
			if (old == NULL) {
				IPFERROR(140028);
				error = ENOMEM;
				break;
			}
			error = COPYIN(obj->ipfo_ptr, old, sizeof(*old));
			if (error != 0) {
				IPFERROR(140029);
			}
			KFREE(old);
		}
		break;

	case IPFOBJ_IPSTATE :
		/*
		 * This structure is not copied in by itself.
		 */
		break;

	case IPFOBJ_STATESTAT :
		/*
		 * Statistics are not copied in.
		 */
		break;

	case IPFOBJ_FRAUTH :
		if (obj->ipfo_rev >= 4013200) {
			frauth_4_1_32_t *old32;

			KMALLOC(old32, frauth_4_1_32_t *);
			if (old32 == NULL) {
				IPFERROR(140030);
				error = ENOMEM;
				break;
			}
			error = COPYIN(obj->ipfo_ptr, old32, sizeof(*old32));
			if (error == 0) {
				frauth_4_1_32_to_current(old32, ptr);
			} else {
				IPFERROR(140031);
			}
			KFREE(old32);
		} else if (obj->ipfo_rev >= 4012900) {
			frauth_4_1_29_t *old29;

			KMALLOC(old29, frauth_4_1_29_t *);
			if (old29 == NULL) {
				IPFERROR(140032);
				error = ENOMEM;
				break;
			}
			error = COPYIN(obj->ipfo_ptr, old29, sizeof(*old29));
			if (error == 0) {
				frauth_4_1_29_to_current(old29, ptr);
			} else {
				IPFERROR(140033);
			}
			KFREE(old29);
		} else if (obj->ipfo_rev >= 4012400) {
			frauth_4_1_24_t *old24;

			KMALLOC(old24, frauth_4_1_24_t *);
			if (old24 == NULL) {
				IPFERROR(140034);
				error = ENOMEM;
				break;
			}
			error = COPYIN(obj->ipfo_ptr, old24, sizeof(*old24));
			if (error == 0) {
				frauth_4_1_24_to_current(old24, ptr);
			} else {
				IPFERROR(140035);
			}
			KFREE(old24);
		} else if (obj->ipfo_rev >= 4012300) {
			frauth_4_1_23_t *old23;

			KMALLOC(old23, frauth_4_1_23_t *);
			if (old23 == NULL) {
				IPFERROR(140036);
				error = ENOMEM;
				break;
			}
			error = COPYIN(obj->ipfo_ptr, old23, sizeof(*old23));
			if (error == 0)
				frauth_4_1_23_to_current(old23, ptr);
			KFREE(old23);
		} else if (obj->ipfo_rev >= 4011100) {
			frauth_4_1_11_t *old11;

			KMALLOC(old11, frauth_4_1_11_t *);
			if (old11 == NULL) {
				IPFERROR(140037);
				error = ENOMEM;
				break;
			}
			error = COPYIN(obj->ipfo_ptr, old11, sizeof(*old11));
			if (error == 0) {
				frauth_4_1_11_to_current(old11, ptr);
			} else {
				IPFERROR(140038);
			}
			KFREE(old11);
		}
		break;

	case IPFOBJ_NAT :
		if (obj->ipfo_rev >= 4011400) {
			sz = sizeof(nat_4_1_14_t);
		} else if (obj->ipfo_rev >= 4010300) {
			sz = sizeof(nat_4_1_3_t);
		} else {
			break;
		}
		bzero(ptr, sizeof(nat_t));
		error = COPYIN(obj->ipfo_ptr, ptr, sz);
		if (error != 0) {
			IPFERROR(140039);
		}
		break;

	case IPFOBJ_FRIPF :
		if (obj->ipfo_rev < 5000000) {
			fripf4_t *old;

			KMALLOC(old, fripf4_t *);
			if (old == NULL) {
				IPFERROR(140040);
				error = ENOMEM;
				break;
			}
			error = COPYIN(obj->ipfo_ptr, old, sizeof(*old));
			if (error == 0) {
				ipf_v4fripftov5(old, ptr);
			} else {
				IPFERROR(140041);
			}
			KFREE(old);
		}
		break;
	}

	return error;
}
/* ------------------------------------------------------------------------ */


/*
 * flags is v4 flags, returns v5 flags.
 */
static int
fr_frflags4to5(flags)
	u_32_t flags;
{
	u_32_t nflags = 0;

	switch (flags & 0xf) {
	case 0x0 :
		nflags |= FR_CALL;
		break;
	case 0x1 :
		nflags |= FR_BLOCK;
		break;
	case 0x2 :
		nflags |= FR_PASS;
		break;
	case 0x3 :
		nflags |= FR_AUTH;
		break;
	case 0x4 :
		nflags |= FR_PREAUTH;
		break;
	case 0x5 :
		nflags |= FR_ACCOUNT;
		break;
	case 0x6 :
		nflags |= FR_SKIP;
		break;
	default :
		break;
	}

	if (flags & 0x00010)
		nflags |= FR_LOG;
	if (flags & 0x00020)
		nflags |= FR_CALLNOW;
	if (flags & 0x00080)
		nflags |= FR_NOTSRCIP;
	if (flags & 0x00040)
		nflags |= FR_NOTDSTIP;
	if (flags & 0x00100)
		nflags |= FR_QUICK;
	if (flags & 0x00200)
		nflags |= FR_KEEPFRAG;
	if (flags & 0x00400)
		nflags |= FR_KEEPSTATE;
	if (flags & 0x00800)
		nflags |= FR_FASTROUTE;
	if (flags & 0x01000)
		nflags |= FR_RETRST;
	if (flags & 0x02000)
		nflags |= FR_RETICMP;
	if (flags & 0x03000)
		nflags |= FR_FAKEICMP;
	if (flags & 0x04000)
		nflags |= FR_OUTQUE;
	if (flags & 0x08000)
		nflags |= FR_INQUE;
	if (flags & 0x10000)
		nflags |= FR_LOGBODY;
	if (flags & 0x20000)
		nflags |= FR_LOGFIRST;
	if (flags & 0x40000)
		nflags |= FR_LOGORBLOCK;
	if (flags & 0x100000)
		nflags |= FR_FRSTRICT;
	if (flags & 0x200000)
		nflags |= FR_STSTRICT;
	if (flags & 0x400000)
		nflags |= FR_NEWISN;
	if (flags & 0x800000)
		nflags |= FR_NOICMPERR;
	if (flags & 0x1000000)
		nflags |= FR_STATESYNC;
	if (flags & 0x8000000)
		nflags |= FR_NOMATCH;
	if (flags & 0x40000000)
		nflags |= FR_COPIED;
	if (flags & 0x80000000)
		nflags |= FR_INACTIVE;

	return nflags;
}

static void
frentry_4_1_34_to_current(softc, old, current, size)
	ipf_main_softc_t *softc;
	frentry_4_1_34_t *old;
	void *current;
	int size;
{
	frentry_t *fr = (frentry_t *)current;

	fr->fr_comment = -1;
	fr->fr_ref = old->fr_ref;
	fr->fr_statecnt = old->fr_statecnt;
	fr->fr_hits = old->fr_hits;
	fr->fr_bytes = old->fr_bytes;
	fr->fr_lastpkt.tv_sec = old->fr_lastpkt.tv_sec;
	fr->fr_lastpkt.tv_usec = old->fr_lastpkt.tv_usec;
	bcopy(&old->fr_dun, &fr->fr_dun, sizeof(old->fr_dun));
	fr->fr_func = old->fr_func;
	fr->fr_dsize = old->fr_dsize;
	fr->fr_pps = old->fr_pps;
	fr->fr_statemax = old->fr_statemax;
	fr->fr_flineno = old->fr_flineno;
	fr->fr_type = old->fr_type;
	fr->fr_flags = fr_frflags4to5(old->fr_flags);
	fr->fr_logtag = old->fr_logtag;
	fr->fr_collect = old->fr_collect;
	fr->fr_arg = old->fr_arg;
	fr->fr_loglevel = old->fr_loglevel;
	fr->fr_age[0] = old->fr_age[0];
	fr->fr_age[1] = old->fr_age[1];
	fr->fr_tifs[0].fd_ip6 = old->fr_tifs[0].ofd_ip6;
	fr->fr_tifs[0].fd_type = FRD_NORMAL;
	fr->fr_tifs[1].fd_ip6 = old->fr_tifs[1].ofd_ip6;
	fr->fr_tifs[1].fd_type = FRD_NORMAL;
	fr->fr_dif.fd_ip6 = old->fr_dif.ofd_ip6;
	fr->fr_dif.fd_type = FRD_NORMAL;
	if (old->fr_v == 4)
		fr->fr_family = AF_INET;
	if (old->fr_v == 6)
		fr->fr_family = AF_INET6;
	fr->fr_icode = old->fr_icode;
	fr->fr_cksum = old->fr_cksum;
	fr->fr_namelen = 0;
	fr->fr_ifnames[0] = -1;
	fr->fr_ifnames[1] = -1;
	fr->fr_ifnames[2] = -1;
	fr->fr_ifnames[3] = -1;
	fr->fr_dif.fd_name = -1;
	fr->fr_tifs[0].fd_name = -1;
	fr->fr_tifs[1].fd_name = -1;
	fr->fr_group = -1;
	fr->fr_grhead = -1;
	fr->fr_icmphead = -1;
	if (size == 0) {
		fr->fr_size = sizeof(*fr) + LIFNAMSIZ * 7 + FR_GROUPLEN * 2;
		fr->fr_size += sizeof(fripf_t) + 16;
		fr->fr_size += 9;	/* room for \0's */
	} else {
		char *names = fr->fr_names;
		int nlen = fr->fr_namelen;

		fr->fr_size = size;
		if (old->fr_ifnames[0][0] != '\0') {
			fr->fr_ifnames[0] = nlen;
			nlen = ipf_addfrstr(names, nlen, old->fr_ifnames[0],
					    LIFNAMSIZ);
		}
		if (old->fr_ifnames[1][0] != '\0') {
			fr->fr_ifnames[1] = nlen;
			nlen = ipf_addfrstr(names, nlen, old->fr_ifnames[1],
					    LIFNAMSIZ);
		}
		if (old->fr_ifnames[2][0] != '\0') {
			fr->fr_ifnames[2] = nlen;
			nlen = ipf_addfrstr(names, nlen, old->fr_ifnames[2],
					    LIFNAMSIZ);
		}
		if (old->fr_ifnames[3][0] != '\0') {
			fr->fr_ifnames[3] = nlen;
			nlen = ipf_addfrstr(names, nlen, old->fr_ifnames[3],
					    LIFNAMSIZ);
		}
		if (old->fr_tifs[0].fd_ifname[0] != '\0') {
			fr->fr_tifs[0].fd_name = nlen;
			nlen = ipf_addfrstr(names, nlen,
					    old->fr_tifs[0].fd_ifname,
					    LIFNAMSIZ);
		}
		if (old->fr_tifs[1].fd_ifname[0] != '\0') {
			fr->fr_tifs[1].fd_name = nlen;
			nlen = ipf_addfrstr(names, nlen,
					    old->fr_tifs[1].fd_ifname,
					    LIFNAMSIZ);
		}
		if (old->fr_dif.fd_ifname[0] != '\0') {
			fr->fr_dif.fd_name = nlen;
			nlen = ipf_addfrstr(names, nlen,
					    old->fr_dif.fd_ifname, LIFNAMSIZ);
		}
		if (old->fr_group[0] != '\0') {
			fr->fr_group = nlen;
			nlen = ipf_addfrstr(names, nlen,
					    old->fr_group, LIFNAMSIZ);
		}
		if (old->fr_grhead[0] != '\0') {
			fr->fr_grhead = nlen;
			nlen = ipf_addfrstr(names, nlen,
					    old->fr_grhead, LIFNAMSIZ);
		}
		fr->fr_namelen = nlen;

		if (old->fr_type == FR_T_IPF) {
			int offset = fr->fr_namelen;
			ipfobj_t obj;
			int error;

			obj.ipfo_type = IPFOBJ_FRIPF;
			obj.ipfo_rev = 4010100;
			obj.ipfo_ptr = old->fr_data;

			if ((offset & 7) != 0)
				offset += 8 - (offset & 7);
			error = ipf_in_compat(softc, &obj,
					      fr->fr_names + offset, 0);
			if (error == 0) {
				fr->fr_data = fr->fr_names + offset;
				fr->fr_dsize = sizeof(fripf_t);
			}
		}
	}
}

static void
frentry_4_1_16_to_current(softc, old, current, size)
	ipf_main_softc_t *softc;
	frentry_4_1_16_t *old;
	void *current;
	int size;
{
	frentry_t *fr = (frentry_t *)current;

	fr->fr_comment = -1;
	fr->fr_ref = old->fr_ref;
	fr->fr_statecnt = old->fr_statecnt;
	fr->fr_hits = old->fr_hits;
	fr->fr_bytes = old->fr_bytes;
	fr->fr_lastpkt.tv_sec = old->fr_lastpkt.tv_sec;
	fr->fr_lastpkt.tv_usec = old->fr_lastpkt.tv_usec;
	bcopy(&old->fr_dun, &fr->fr_dun, sizeof(old->fr_dun));
	fr->fr_func = old->fr_func;
	fr->fr_dsize = old->fr_dsize;
	fr->fr_pps = old->fr_pps;
	fr->fr_statemax = old->fr_statemax;
	fr->fr_flineno = old->fr_flineno;
	fr->fr_type = old->fr_type;
	fr->fr_flags = fr_frflags4to5(old->fr_flags);
	fr->fr_logtag = old->fr_logtag;
	fr->fr_collect = old->fr_collect;
	fr->fr_arg = old->fr_arg;
	fr->fr_loglevel = old->fr_loglevel;
	fr->fr_age[0] = old->fr_age[0];
	fr->fr_age[1] = old->fr_age[1];
	fr->fr_tifs[0].fd_ip6 = old->fr_tifs[0].ofd_ip6;
	fr->fr_tifs[0].fd_type = FRD_NORMAL;
	fr->fr_tifs[1].fd_ip6 = old->fr_tifs[1].ofd_ip6;
	fr->fr_tifs[1].fd_type = FRD_NORMAL;
	fr->fr_dif.fd_ip6 = old->fr_dif.ofd_ip6;
	fr->fr_dif.fd_type = FRD_NORMAL;
	if (old->fr_v == 4)
		fr->fr_family = AF_INET;
	if (old->fr_v == 6)
		fr->fr_family = AF_INET6;
	fr->fr_icode = old->fr_icode;
	fr->fr_cksum = old->fr_cksum;
	fr->fr_namelen = 0;
	fr->fr_ifnames[0] = -1;
	fr->fr_ifnames[1] = -1;
	fr->fr_ifnames[2] = -1;
	fr->fr_ifnames[3] = -1;
	fr->fr_dif.fd_name = -1;
	fr->fr_tifs[0].fd_name = -1;
	fr->fr_tifs[1].fd_name = -1;
	fr->fr_group = -1;
	fr->fr_grhead = -1;
	fr->fr_icmphead = -1;
	if (size == 0) {
		fr->fr_size = sizeof(*fr) + LIFNAMSIZ * 7 + FR_GROUPLEN * 2;
		fr->fr_size += 9;	/* room for \0's */
	} else {
		char *names = fr->fr_names;
		int nlen = fr->fr_namelen;

		fr->fr_size = size;
		if (old->fr_ifnames[0][0] != '\0') {
			fr->fr_ifnames[0] = nlen;
			nlen = ipf_addfrstr(names, nlen, old->fr_ifnames[0],
					    LIFNAMSIZ);
		}
		if (old->fr_ifnames[1][0] != '\0') {
			fr->fr_ifnames[1] = nlen;
			nlen = ipf_addfrstr(names, nlen, old->fr_ifnames[1],
					    LIFNAMSIZ);
		}
		if (old->fr_ifnames[2][0] != '\0') {
			fr->fr_ifnames[2] = nlen;
			nlen = ipf_addfrstr(names, nlen, old->fr_ifnames[2],
					    LIFNAMSIZ);
		}
		if (old->fr_ifnames[3][0] != '\0') {
			fr->fr_ifnames[3] = nlen;
			nlen = ipf_addfrstr(names, nlen, old->fr_ifnames[3],
					    LIFNAMSIZ);
		}
		if (old->fr_tifs[0].fd_ifname[0] != '\0') {
			fr->fr_tifs[0].fd_name = nlen;
			nlen = ipf_addfrstr(names, nlen,
					    old->fr_tifs[0].fd_ifname,
					    LIFNAMSIZ);
		}
		if (old->fr_tifs[1].fd_ifname[0] != '\0') {
			fr->fr_tifs[1].fd_name = nlen;
			nlen = ipf_addfrstr(names, nlen,
					    old->fr_tifs[1].fd_ifname,
					    LIFNAMSIZ);
		}
		if (old->fr_dif.fd_ifname[0] != '\0') {
			fr->fr_dif.fd_name = nlen;
			nlen = ipf_addfrstr(names, nlen,
					    old->fr_dif.fd_ifname, LIFNAMSIZ);
		}
		if (old->fr_group[0] != '\0') {
			fr->fr_group = nlen;
			nlen = ipf_addfrstr(names, nlen,
					    old->fr_group, LIFNAMSIZ);
		}
		if (old->fr_grhead[0] != '\0') {
			fr->fr_grhead = nlen;
			nlen = ipf_addfrstr(names, nlen,
					    old->fr_grhead, LIFNAMSIZ);
		}
		fr->fr_namelen = nlen;

		if (old->fr_type == FR_T_IPF) {
			int offset = fr->fr_namelen;
			ipfobj_t obj;
			int error;

			obj.ipfo_type = IPFOBJ_FRIPF;
			obj.ipfo_rev = 4010100;
			obj.ipfo_ptr = old->fr_data;

			if ((offset & 7) != 0)
				offset += 8 - (offset & 7);
			error = ipf_in_compat(softc, &obj,
					      fr->fr_names + offset, 0);
			if (error == 0) {
				fr->fr_data = fr->fr_names + offset;
				fr->fr_dsize = sizeof(fripf_t);
			}
		}
	}
}


static void
frentry_4_1_0_to_current(softc, old, current, size)
	ipf_main_softc_t *softc;
	frentry_4_1_0_t *old;
	void *current;
	int size;
{
	frentry_t *fr = (frentry_t *)current;

	fr->fr_size = sizeof(*fr);
	fr->fr_comment = -1;
	fr->fr_ref = old->fr_ref;
	fr->fr_statecnt = old->fr_statecnt;
	fr->fr_hits = old->fr_hits;
	fr->fr_bytes = old->fr_bytes;
	fr->fr_lastpkt.tv_sec = old->fr_lastpkt.tv_sec;
	fr->fr_lastpkt.tv_usec = old->fr_lastpkt.tv_usec;
	bcopy(&old->fr_dun, &fr->fr_dun, sizeof(old->fr_dun));
	fr->fr_func = old->fr_func;
	fr->fr_dsize = old->fr_dsize;
	fr->fr_pps = old->fr_pps;
	fr->fr_statemax = old->fr_statemax;
	fr->fr_flineno = old->fr_flineno;
	fr->fr_type = old->fr_type;
	fr->fr_flags = fr_frflags4to5(old->fr_flags);
	fr->fr_logtag = old->fr_logtag;
	fr->fr_collect = old->fr_collect;
	fr->fr_arg = old->fr_arg;
	fr->fr_loglevel = old->fr_loglevel;
	fr->fr_age[0] = old->fr_age[0];
	fr->fr_age[1] = old->fr_age[1];
	fr->fr_tifs[0].fd_ip6 = old->fr_tifs[0].ofd_ip6;
	fr->fr_tifs[0].fd_type = FRD_NORMAL;
	fr->fr_tifs[1].fd_ip6 = old->fr_tifs[1].ofd_ip6;
	fr->fr_tifs[1].fd_type = FRD_NORMAL;
	fr->fr_dif.fd_ip6 = old->fr_dif.ofd_ip6;
	fr->fr_dif.fd_type = FRD_NORMAL;
	if (old->fr_v == 4)
		fr->fr_family = AF_INET;
	if (old->fr_v == 6)
		fr->fr_family = AF_INET6;
	fr->fr_icode = old->fr_icode;
	fr->fr_cksum = old->fr_cksum;
	fr->fr_namelen = 0;
	fr->fr_ifnames[0] = -1;
	fr->fr_ifnames[1] = -1;
	fr->fr_ifnames[2] = -1;
	fr->fr_ifnames[3] = -1;
	fr->fr_dif.fd_name = -1;
	fr->fr_tifs[0].fd_name = -1;
	fr->fr_tifs[1].fd_name = -1;
	fr->fr_group = -1;
	fr->fr_grhead = -1;
	fr->fr_icmphead = -1;
	if (size == 0) {
		fr->fr_size = sizeof(*fr) + LIFNAMSIZ * 7 + FR_GROUPLEN * 2;
		fr->fr_size += 9;	/* room for \0's */
	} else {
		char *names = fr->fr_names;
		int nlen = fr->fr_namelen;

		fr->fr_size = size;
		if (old->fr_ifnames[0][0] != '\0') {
			fr->fr_ifnames[0] = nlen;
			nlen = ipf_addfrstr(names, nlen, old->fr_ifnames[0],
					    LIFNAMSIZ);
		}
		if (old->fr_ifnames[1][0] != '\0') {
			fr->fr_ifnames[1] = nlen;
			nlen = ipf_addfrstr(names, nlen, old->fr_ifnames[1],
					    LIFNAMSIZ);
		}
		if (old->fr_ifnames[2][0] != '\0') {
			fr->fr_ifnames[2] = nlen;
			nlen = ipf_addfrstr(names, nlen, old->fr_ifnames[2],
					    LIFNAMSIZ);
		}
		if (old->fr_ifnames[3][0] != '\0') {
			fr->fr_ifnames[3] = nlen;
			nlen = ipf_addfrstr(names, nlen, old->fr_ifnames[3],
					    LIFNAMSIZ);
		}
		if (old->fr_tifs[0].fd_ifname[0] != '\0') {
			fr->fr_tifs[0].fd_name = nlen;
			nlen = ipf_addfrstr(names, nlen,
					    old->fr_tifs[0].fd_ifname,
					    LIFNAMSIZ);
		}
		if (old->fr_tifs[1].fd_ifname[0] != '\0') {
			fr->fr_tifs[1].fd_name = nlen;
			nlen = ipf_addfrstr(names, nlen,
					    old->fr_tifs[1].fd_ifname,
					    LIFNAMSIZ);
		}
		if (old->fr_dif.fd_ifname[0] != '\0') {
			fr->fr_dif.fd_name = nlen;
			nlen = ipf_addfrstr(names, nlen,
					    old->fr_dif.fd_ifname, LIFNAMSIZ);
		}
		if (old->fr_group[0] != '\0') {
			fr->fr_group = nlen;
			nlen = ipf_addfrstr(names, nlen,
					    old->fr_group, LIFNAMSIZ);
		}
		if (old->fr_grhead[0] != '\0') {
			fr->fr_grhead = nlen;
			nlen = ipf_addfrstr(names, nlen,
					    old->fr_grhead, LIFNAMSIZ);
		}
		fr->fr_namelen = nlen;

		if (old->fr_type == FR_T_IPF) {
			int offset = fr->fr_namelen;
			ipfobj_t obj;
			int error;

			obj.ipfo_type = IPFOBJ_FRIPF;
			obj.ipfo_rev = 4010100;
			obj.ipfo_ptr = old->fr_data;

			if ((offset & 7) != 0)
				offset += 8 - (offset & 7);
				offset += 8 - (offset & 7);
			error = ipf_in_compat(softc, &obj,
					      fr->fr_names + offset, 0);
			if (error == 0) {
				fr->fr_data = fr->fr_names + offset;
				fr->fr_dsize = sizeof(fripf_t);
			}
		}
	}
}


static void
friostat_4_1_33_to_current(old, current)
	friostat_4_1_33_t *old;
	void *current;
{
	friostat_t *fiop = (friostat_t *)current;

	bcopy(&old->of_st[0], &fiop->f_st[0].fr_pass, sizeof(old->of_st[0]));
	bcopy(&old->of_st[1], &fiop->f_st[1].fr_pass, sizeof(old->of_st[1]));

	fiop->f_ipf[0][0] = old->f_ipf[0][0];
	fiop->f_ipf[0][1] = old->f_ipf[0][1];
	fiop->f_ipf[1][0] = old->f_ipf[1][0];
	fiop->f_ipf[1][1] = old->f_ipf[1][1];
	fiop->f_acct[0][0] = old->f_acct[0][0];
	fiop->f_acct[0][1] = old->f_acct[0][1];
	fiop->f_acct[1][0] = old->f_acct[1][0];
	fiop->f_acct[1][1] = old->f_acct[1][1];
	fiop->f_auth = fiop->f_auth;
	bcopy(&old->f_groups, &fiop->f_groups, sizeof(old->f_groups));
	bcopy(&old->f_froute, &fiop->f_froute, sizeof(old->f_froute));
	fiop->f_ticks = old->f_ticks;
	bcopy(&old->f_locks, &fiop->f_locks, sizeof(old->f_locks));
	fiop->f_defpass = old->f_defpass;
	fiop->f_active = old->f_active;
	fiop->f_running = old->f_running;
	fiop->f_logging = old->f_logging;
	fiop->f_features = old->f_features;
	bcopy(old->f_version, fiop->f_version, sizeof(old->f_version));
}


static void
friostat_4_1_0_to_current(old, current)
	friostat_4_1_0_t *old;
	void *current;
{
	friostat_t *fiop = (friostat_t *)current;

	bcopy(&old->of_st[0], &fiop->f_st[0].fr_pass, sizeof(old->of_st[0]));
	bcopy(&old->of_st[1], &fiop->f_st[1].fr_pass, sizeof(old->of_st[1]));

	fiop->f_ipf[0][0] = old->f_ipf[0][0];
	fiop->f_ipf[0][1] = old->f_ipf[0][1];
	fiop->f_ipf[1][0] = old->f_ipf[1][0];
	fiop->f_ipf[1][1] = old->f_ipf[1][1];
	fiop->f_acct[0][0] = old->f_acct[0][0];
	fiop->f_acct[0][1] = old->f_acct[0][1];
	fiop->f_acct[1][0] = old->f_acct[1][0];
	fiop->f_acct[1][1] = old->f_acct[1][1];
	fiop->f_auth = fiop->f_auth;
	bcopy(&old->f_groups, &fiop->f_groups, sizeof(old->f_groups));
	bcopy(&old->f_froute, &fiop->f_froute, sizeof(old->f_froute));
	fiop->f_ticks = old->f_ticks;
	bcopy(&old->f_locks, &fiop->f_locks, sizeof(old->f_locks));
	fiop->f_defpass = old->f_defpass;
	fiop->f_active = old->f_active;
	fiop->f_running = old->f_running;
	fiop->f_logging = old->f_logging;
	fiop->f_features = old->f_features;
	bcopy(old->f_version, fiop->f_version, sizeof(old->f_version));
}


static void
ipnat_4_1_14_to_current(old, current, size)
	ipnat_4_1_14_t *old;
	void *current;
	int size;
{
	ipnat_t *np = (ipnat_t *)current;

	np->in_space = old->in_space;
	np->in_hv[0] = old->in_hv;
	np->in_hv[1] = old->in_hv;
	np->in_flineno = old->in_flineno;
	if (old->in_redir == NAT_REDIRECT)
		np->in_dpnext = old->in_pnext;
	else
		np->in_spnext = old->in_pnext;
	np->in_v[0] = old->in_v;
	np->in_v[1] = old->in_v;
	np->in_flags = old->in_flags;
	np->in_mssclamp = old->in_mssclamp;
	np->in_age[0] = old->in_age[0];
	np->in_age[1] = old->in_age[1];
	np->in_redir = old->in_redir;
	np->in_pr[0] = old->in_p;
	np->in_pr[1] = old->in_p;
	if (np->in_redir == NAT_REDIRECT) {
		np->in_ndst.na_nextaddr = old->in_next6;
		np->in_ndst.na_addr[0] = old->in_in[0];
		np->in_ndst.na_addr[1] = old->in_in[1];
		np->in_ndst.na_atype = FRI_NORMAL;
		np->in_odst.na_addr[0] = old->in_out[0];
		np->in_odst.na_addr[1] = old->in_out[1];
		np->in_odst.na_atype = FRI_NORMAL;
		np->in_osrc.na_addr[0] = old->in_src[0];
		np->in_osrc.na_addr[1] = old->in_src[1];
		np->in_osrc.na_atype = FRI_NORMAL;
	} else {
		np->in_nsrc.na_nextaddr = old->in_next6;
		np->in_nsrc.na_addr[0] = old->in_out[0];
		np->in_nsrc.na_addr[1] = old->in_out[1];
		np->in_nsrc.na_atype = FRI_NORMAL;
		np->in_osrc.na_addr[0] = old->in_in[0];
		np->in_osrc.na_addr[1] = old->in_in[1];
		np->in_osrc.na_atype = FRI_NORMAL;
		np->in_odst.na_addr[0] = old->in_src[0];
		np->in_odst.na_addr[1] = old->in_src[1];
		np->in_odst.na_atype = FRI_NORMAL;
	}
	ipfv4tuctov5(&old->in_tuc, &np->in_tuc);
	if (np->in_redir == NAT_REDIRECT) {
		np->in_dpmin = old->in_port[0];
		np->in_dpmax = old->in_port[1];
	} else {
		np->in_spmin = old->in_port[0];
		np->in_spmax = old->in_port[1];
	}
	np->in_ppip = old->in_ppip;
	np->in_ippip = old->in_ippip;
	np->in_tag = old->in_tag;

	np->in_namelen = 0;
	np->in_plabel = -1;
	np->in_ifnames[0] = -1;
	np->in_ifnames[1] = -1;

	if (size == 0) {
		np->in_size = sizeof(*np);
		np->in_size += LIFNAMSIZ * 2 + APR_LABELLEN;
		np->in_size += 3;
	} else {
		int nlen = np->in_namelen;
		char *names = np->in_names;

		if (old->in_ifnames[0][0] != '\0') {
			np->in_ifnames[0] = nlen;
			nlen = ipf_addfrstr(names, nlen, old->in_ifnames[0],
					    LIFNAMSIZ);
		}
		if (old->in_ifnames[1][0] != '\0') {
			np->in_ifnames[0] = nlen;
			nlen = ipf_addfrstr(names, nlen, old->in_ifnames[1],
					    LIFNAMSIZ);
		}
		if (old->in_plabel[0] != '\0') {
			np->in_plabel = nlen;
			nlen = ipf_addfrstr(names, nlen, old->in_plabel,
					    LIFNAMSIZ);
		}
		np->in_namelen = nlen;
		np->in_size = size;
	}
}


static void
ipnat_4_1_0_to_current(old, current, size)
	ipnat_4_1_0_t *old;
	void *current;
	int size;
{
	ipnat_t *np = (ipnat_t *)current;

	np->in_space = old->in_space;
	np->in_hv[0] = old->in_hv;
	np->in_hv[1] = old->in_hv;
	np->in_flineno = old->in_flineno;
	if (old->in_redir == NAT_REDIRECT)
		np->in_dpnext = old->in_pnext;
	else
		np->in_spnext = old->in_pnext;
	np->in_v[0] = old->in_v;
	np->in_v[1] = old->in_v;
	np->in_flags = old->in_flags;
	np->in_mssclamp = old->in_mssclamp;
	np->in_age[0] = old->in_age[0];
	np->in_age[1] = old->in_age[1];
	np->in_redir = old->in_redir;
	np->in_pr[0] = old->in_p;
	np->in_pr[1] = old->in_p;
	if (np->in_redir == NAT_REDIRECT) {
		np->in_ndst.na_nextaddr = old->in_next6;
		bcopy(&old->in_in, &np->in_ndst.na_addr, sizeof(old->in_in));
		bcopy(&old->in_out, &np->in_odst.na_addr, sizeof(old->in_out));
		bcopy(&old->in_src, &np->in_osrc.na_addr, sizeof(old->in_src));
	} else {
		np->in_nsrc.na_nextaddr = old->in_next6;
		bcopy(&old->in_in, &np->in_osrc.na_addr, sizeof(old->in_in));
		bcopy(&old->in_out, &np->in_nsrc.na_addr, sizeof(old->in_out));
		bcopy(&old->in_src, &np->in_odst.na_addr, sizeof(old->in_src));
	}
	ipfv4tuctov5(&old->in_tuc, &np->in_tuc);
	if (np->in_redir == NAT_REDIRECT) {
		np->in_dpmin = old->in_port[0];
		np->in_dpmax = old->in_port[1];
	} else {
		np->in_spmin = old->in_port[0];
		np->in_spmax = old->in_port[1];
	}
	np->in_ppip = old->in_ppip;
	np->in_ippip = old->in_ippip;
	bcopy(&old->in_tag, &np->in_tag, sizeof(np->in_tag));

	np->in_namelen = 0;
	np->in_plabel = -1;
	np->in_ifnames[0] = -1;
	np->in_ifnames[1] = -1;

	if (size == 0) {
		np->in_size = sizeof(*np);
		np->in_size += LIFNAMSIZ * 2 + APR_LABELLEN;
		np->in_size += 3;
	} else {
		int nlen = np->in_namelen;
		char *names = np->in_names;

		if (old->in_ifnames[0][0] != '\0') {
			np->in_ifnames[0] = nlen;
			nlen = ipf_addfrstr(names, nlen, old->in_ifnames[0],
					    LIFNAMSIZ);
		}
		if (old->in_ifnames[1][0] != '\0') {
			np->in_ifnames[0] = nlen;
			nlen = ipf_addfrstr(names, nlen, old->in_ifnames[1],
					    LIFNAMSIZ);
		}
		if (old->in_plabel[0] != '\0') {
			np->in_plabel = nlen;
			nlen = ipf_addfrstr(names, nlen, old->in_plabel,
					    LIFNAMSIZ);
		}
		np->in_namelen = nlen;
		np->in_size = size;
	}
}


static void
frauth_4_1_32_to_current(old, current)
	frauth_4_1_32_t *old;
	void *current;
{
	frauth_t *fra = (frauth_t *)current;

	fra->fra_age = old->fra_age;
	fra->fra_len = old->fra_len;
	fra->fra_index = old->fra_index;
	fra->fra_pass = old->fra_pass;
	fr_info_4_1_32_to_current(&old->fra_info, &fra->fra_info);
	fra->fra_buf = old->fra_buf;
	fra->fra_flx = old->fra_flx;
#ifdef	MENTAT
	fra->fra_q = old->fra_q;
	fra->fra_m = old->fra_m;
#endif
}


static void
frauth_4_1_29_to_current(old, current)
	frauth_4_1_29_t *old;
	void *current;
{
	frauth_t *fra = (frauth_t *)current;

	fra->fra_age = old->fra_age;
	fra->fra_len = old->fra_len;
	fra->fra_index = old->fra_index;
	fra->fra_pass = old->fra_pass;
	fr_info_4_1_24_to_current(&old->fra_info, &fra->fra_info);
	fra->fra_buf = old->fra_buf;
	fra->fra_flx = old->fra_flx;
#ifdef	MENTAT
	fra->fra_q = old->fra_q;
	fra->fra_m = old->fra_m;
#endif
}


static void
frauth_4_1_24_to_current(old, current)
	frauth_4_1_24_t *old;
	void *current;
{
	frauth_t *fra = (frauth_t *)current;

	fra->fra_age = old->fra_age;
	fra->fra_len = old->fra_len;
	fra->fra_index = old->fra_index;
	fra->fra_pass = old->fra_pass;
	fr_info_4_1_24_to_current(&old->fra_info, &fra->fra_info);
	fra->fra_buf = old->fra_buf;
#ifdef	MENTAT
	fra->fra_q = old->fra_q;
	fra->fra_m = old->fra_m;
#endif
}


static void
frauth_4_1_23_to_current(old, current)
	frauth_4_1_23_t *old;
	void *current;
{
	frauth_t *fra = (frauth_t *)current;

	fra->fra_age = old->fra_age;
	fra->fra_len = old->fra_len;
	fra->fra_index = old->fra_index;
	fra->fra_pass = old->fra_pass;
	fr_info_4_1_23_to_current(&old->fra_info, &fra->fra_info);
	fra->fra_buf = old->fra_buf;
#ifdef	MENTAT
	fra->fra_q = old->fra_q;
	fra->fra_m = old->fra_m;
#endif
}


static void
frauth_4_1_11_to_current(old, current)
	frauth_4_1_11_t *old;
	void *current;
{
	frauth_t *fra = (frauth_t *)current;

	fra->fra_age = old->fra_age;
	fra->fra_len = old->fra_len;
	fra->fra_index = old->fra_index;
	fra->fra_pass = old->fra_pass;
	fr_info_4_1_11_to_current(&old->fra_info, &fra->fra_info);
	fra->fra_buf = old->fra_buf;
#ifdef	MENTAT
	fra->fra_q = old->fra_q;
	fra->fra_m = old->fra_m;
#endif
}


static void
fr_info_4_1_32_to_current(old, current)
	fr_info_4_1_32_t *old;
	void *current;
{
	fr_info_t *fin = (fr_info_t *)current;

	fin->fin_ifp = old->fin_ifp;
	ipf_v4iptov5(&old->fin_fi, &fin->fin_fi);
	bcopy(&old->fin_dat, &fin->fin_dat, sizeof(old->fin_dat));
	fin->fin_out = old->fin_out;
	fin->fin_rev = old->fin_rev;
	fin->fin_hlen = old->fin_hlen;
	fin->fin_tcpf = old->ofin_tcpf;
	fin->fin_icode = old->fin_icode;
	fin->fin_rule = old->fin_rule;
	bcopy(old->fin_group, fin->fin_group, sizeof(old->fin_group));
	fin->fin_fr = old->fin_fr;
	fin->fin_dp = old->fin_dp;
	fin->fin_dlen = old->fin_dlen;
	fin->fin_plen = old->fin_plen;
	fin->fin_ipoff = old->fin_ipoff;
	fin->fin_id = old->fin_id;
	fin->fin_off = old->fin_off;
	fin->fin_depth = old->fin_depth;
	fin->fin_error = old->fin_error;
	fin->fin_cksum = old->fin_cksum;
	fin->fin_nattag = old->fin_nattag;
	fin->fin_ip = old->ofin_ip;
	fin->fin_mp = old->fin_mp;
	fin->fin_m = old->fin_m;
#ifdef  MENTAT
	fin->fin_qfm = old->fin_qfm;
	fin->fin_qpi = old->fin_qpi;
#endif
}


static void
fr_info_4_1_24_to_current(old, current)
	fr_info_4_1_24_t *old;
	void *current;
{
	fr_info_t *fin = (fr_info_t *)current;

	fin->fin_ifp = old->fin_ifp;
	ipf_v4iptov5(&old->fin_fi, &fin->fin_fi);
	bcopy(&old->fin_dat, &fin->fin_dat, sizeof(old->fin_dat));
	fin->fin_out = old->fin_out;
	fin->fin_rev = old->fin_rev;
	fin->fin_hlen = old->fin_hlen;
	fin->fin_tcpf = old->ofin_tcpf;
	fin->fin_icode = old->fin_icode;
	fin->fin_rule = old->fin_rule;
	bcopy(old->fin_group, fin->fin_group, sizeof(old->fin_group));
	fin->fin_fr = old->fin_fr;
	fin->fin_dp = old->fin_dp;
	fin->fin_dlen = old->fin_dlen;
	fin->fin_plen = old->fin_plen;
	fin->fin_ipoff = old->fin_ipoff;
	fin->fin_id = old->fin_id;
	fin->fin_off = old->fin_off;
	fin->fin_depth = old->fin_depth;
	fin->fin_error = old->fin_error;
	fin->fin_cksum = old->fin_cksum;
	fin->fin_nattag = old->fin_nattag;
	fin->fin_ip = old->ofin_ip;
	fin->fin_mp = old->fin_mp;
	fin->fin_m = old->fin_m;
#ifdef  MENTAT
	fin->fin_qfm = old->fin_qfm;
	fin->fin_qpi = old->fin_qpi;
#endif
}


static void
fr_info_4_1_23_to_current(old, current)
	fr_info_4_1_23_t *old;
	void *current;
{
	fr_info_t *fin = (fr_info_t *)current;

	fin->fin_ifp = old->fin_ifp;
	ipf_v4iptov5(&old->fin_fi, &fin->fin_fi);
	bcopy(&old->fin_dat, &fin->fin_dat, sizeof(old->fin_dat));
	fin->fin_out = old->fin_out;
	fin->fin_rev = old->fin_rev;
	fin->fin_hlen = old->fin_hlen;
	fin->fin_tcpf = old->ofin_tcpf;
	fin->fin_icode = old->fin_icode;
	fin->fin_rule = old->fin_rule;
	bcopy(old->fin_group, fin->fin_group, sizeof(old->fin_group));
	fin->fin_fr = old->fin_fr;
	fin->fin_dp = old->fin_dp;
	fin->fin_dlen = old->fin_dlen;
	fin->fin_plen = old->fin_plen;
	fin->fin_ipoff = old->fin_ipoff;
	fin->fin_id = old->fin_id;
	fin->fin_off = old->fin_off;
	fin->fin_depth = old->fin_depth;
	fin->fin_error = old->fin_error;
	fin->fin_nattag = old->fin_nattag;
	fin->fin_ip = old->ofin_ip;
	fin->fin_mp = old->fin_mp;
	fin->fin_m = old->fin_m;
#ifdef  MENTAT
	fin->fin_qfm = old->fin_qfm;
	fin->fin_qpi = old->fin_qpi;
#endif
}


static void
fr_info_4_1_11_to_current(old, current)
	fr_info_4_1_11_t *old;
	void *current;
{
	fr_info_t *fin = (fr_info_t *)current;

	fin->fin_ifp = old->fin_ifp;
	ipf_v4iptov5(&old->fin_fi, &fin->fin_fi);
	bcopy(&old->fin_dat, &fin->fin_dat, sizeof(old->fin_dat));
	fin->fin_out = old->fin_out;
	fin->fin_rev = old->fin_rev;
	fin->fin_hlen = old->fin_hlen;
	fin->fin_tcpf = old->ofin_tcpf;
	fin->fin_icode = old->fin_icode;
	fin->fin_rule = old->fin_rule;
	bcopy(old->fin_group, fin->fin_group, sizeof(old->fin_group));
	fin->fin_fr = old->fin_fr;
	fin->fin_dp = old->fin_dp;
	fin->fin_dlen = old->fin_dlen;
	fin->fin_plen = old->fin_plen;
	fin->fin_ipoff = old->fin_ipoff;
	fin->fin_id = old->fin_id;
	fin->fin_off = old->fin_off;
	fin->fin_depth = old->fin_depth;
	fin->fin_error = old->fin_error;
	fin->fin_nattag = old->fin_nattag;
	fin->fin_ip = old->ofin_ip;
	fin->fin_mp = old->fin_mp;
	fin->fin_m = old->fin_m;
#ifdef  MENTAT
	fin->fin_qfm = old->fin_qfm;
	fin->fin_qpi = old->fin_qpi;
#endif
}


static void
nat_4_1_3_to_current(nat_4_1_3_t *old, nat_t *current)
{
	bzero((void *)current, sizeof(*current));
	bcopy((void *)old, (void *)current, sizeof(*old));
}


static void
nat_4_1_14_to_current(nat_4_1_14_t *old, nat_t *current)
{
	bzero((void *)current, sizeof(*current));
	bcopy((void *)old, (void *)current, sizeof(*old));
}


static void
nat_save_4_1_16_to_current(softc, old, current)
	ipf_main_softc_t *softc;
	nat_save_4_1_16_t *old;
	void *current;
{
	nat_save_t *nats = (nat_save_t *)current;

	nats->ipn_next = old->ipn_next;
	nat_4_1_14_to_current(&old->ipn_nat, &nats->ipn_nat);
	bcopy(&old->ipn_ipnat, &nats->ipn_ipnat, sizeof(old->ipn_ipnat));
	frentry_4_1_16_to_current(softc, &old->ipn_fr, &nats->ipn_fr, 0);
	nats->ipn_dsize = old->ipn_dsize;
	bcopy(old->ipn_data, nats->ipn_data, sizeof(nats->ipn_data));
}


static void
nat_save_4_1_14_to_current(softc, old, current)
	ipf_main_softc_t *softc;
	nat_save_4_1_14_t *old;
	void *current;
{
	nat_save_t *nats = (nat_save_t *)current;

	nats->ipn_next = old->ipn_next;
	nat_4_1_14_to_current(&old->ipn_nat, &nats->ipn_nat);
	bcopy(&old->ipn_ipnat, &nats->ipn_ipnat, sizeof(old->ipn_ipnat));
	frentry_4_1_0_to_current(softc, &old->ipn_fr, &nats->ipn_fr, 0);
	nats->ipn_dsize = old->ipn_dsize;
	bcopy(old->ipn_data, nats->ipn_data, sizeof(nats->ipn_data));
}


static void
nat_save_4_1_3_to_current(softc, old, current)
	ipf_main_softc_t *softc;
	nat_save_4_1_3_t *old;
	void *current;
{
	nat_save_t *nats = (nat_save_t *)current;

	nats->ipn_next = old->ipn_next;
	nat_4_1_3_to_current(&old->ipn_nat, &nats->ipn_nat);
	ipnat_4_1_0_to_current(&old->ipn_ipnat, &nats->ipn_ipnat, 0);
	frentry_4_1_0_to_current(softc, &old->ipn_fr, &nats->ipn_fr, 0);
	nats->ipn_dsize = old->ipn_dsize;
	bcopy(old->ipn_data, nats->ipn_data, sizeof(nats->ipn_data));
}


static void
natstat_current_to_4_1_32(current, old)
	void *current;
	natstat_4_1_32_t *old;
{
	natstat_t *ns = (natstat_t *)current;

	old->ns_mapped[0] = ns->ns_side[0].ns_translated;
	old->ns_mapped[1] = ns->ns_side[1].ns_translated;
	old->ns_rules = ns->ns_side[0].ns_inuse + ns->ns_side[1].ns_inuse;
	old->ns_added = ns->ns_side[0].ns_added + ns->ns_side[1].ns_added;
	old->ns_expire = ns->ns_expire;
	old->ns_inuse = ns->ns_side[0].ns_inuse + ns->ns_side[1].ns_inuse;
	old->ns_logged = ns->ns_log_ok;
	old->ns_logfail = ns->ns_log_fail;
	old->ns_memfail = ns->ns_side[0].ns_memfail + ns->ns_side[1].ns_memfail;
	old->ns_badnat = ns->ns_side[0].ns_badnat + ns->ns_side[1].ns_badnat;
	old->ns_addtrpnt = ns->ns_addtrpnt;
	old->ns_table[0] = ns->ns_side[0].ns_table;
	old->ns_table[1] = ns->ns_side[1].ns_table;
	old->ns_maptable = NULL;
	old->ns_list = ns->ns_list;
	old->ns_apslist = NULL;
	old->ns_wilds = ns->ns_wilds;
	old->ns_nattab_sz = ns->ns_nattab_sz;
	old->ns_nattab_max = ns->ns_nattab_max;
	old->ns_rultab_sz = ns->ns_rultab_sz;
	old->ns_rdrtab_sz = ns->ns_rdrtab_sz;
	old->ns_trpntab_sz = ns->ns_trpntab_sz;
	old->ns_hostmap_sz = 0;
	old->ns_instances = ns->ns_instances;
	old->ns_maplist = ns->ns_maplist;
	old->ns_bucketlen[0] = (u_long *)ns->ns_side[0].ns_bucketlen;
	old->ns_bucketlen[1] = (u_long *)ns->ns_side[1].ns_bucketlen;
	old->ns_ticks = ns->ns_ticks;
	old->ns_orphans = ns->ns_orphans;
	old->ns_uncreate[0][0] = ns->ns_side[0].ns_uncreate[0];
	old->ns_uncreate[0][1] = ns->ns_side[0].ns_uncreate[1];
	old->ns_uncreate[1][0] = ns->ns_side[1].ns_uncreate[0];
	old->ns_uncreate[1][1] = ns->ns_side[1].ns_uncreate[1];
}


static void
natstat_current_to_4_1_27(current, old)
	void *current;
	natstat_4_1_27_t *old;
{
	natstat_t *ns = (natstat_t *)current;

	old->ns_mapped[0] = ns->ns_side[0].ns_translated;
	old->ns_mapped[1] = ns->ns_side[1].ns_translated;
	old->ns_rules = ns->ns_side[0].ns_inuse + ns->ns_side[1].ns_inuse;
	old->ns_added = ns->ns_side[0].ns_added + ns->ns_side[1].ns_added;
	old->ns_expire = ns->ns_expire;
	old->ns_inuse = ns->ns_side[0].ns_inuse + ns->ns_side[1].ns_inuse;
	old->ns_logged = ns->ns_log_ok;
	old->ns_logfail = ns->ns_log_fail;
	old->ns_memfail = ns->ns_side[0].ns_memfail + ns->ns_side[1].ns_memfail;
	old->ns_badnat = ns->ns_side[0].ns_badnat + ns->ns_side[1].ns_badnat;
	old->ns_addtrpnt = ns->ns_addtrpnt;
	old->ns_table[0] = ns->ns_side[0].ns_table;
	old->ns_table[1] = ns->ns_side[1].ns_table;
	old->ns_maptable = NULL;
	old->ns_list = ns->ns_list;
	old->ns_apslist = NULL;
	old->ns_wilds = ns->ns_wilds;
	old->ns_nattab_sz = ns->ns_nattab_sz;
	old->ns_nattab_max = ns->ns_nattab_max;
	old->ns_rultab_sz = ns->ns_rultab_sz;
	old->ns_rdrtab_sz = ns->ns_rdrtab_sz;
	old->ns_trpntab_sz = ns->ns_trpntab_sz;
	old->ns_hostmap_sz = 0;
	old->ns_instances = ns->ns_instances;
	old->ns_maplist = ns->ns_maplist;
	old->ns_bucketlen[0] = (u_long *)ns->ns_side[0].ns_bucketlen;
	old->ns_bucketlen[1] = (u_long *)ns->ns_side[1].ns_bucketlen;
	old->ns_ticks = ns->ns_ticks;
	old->ns_orphans = ns->ns_orphans;
}


static void
natstat_current_to_4_1_16(current, old)
	void *current;
	natstat_4_1_16_t *old;
{
	natstat_t *ns = (natstat_t *)current;

	old->ns_mapped[0] = ns->ns_side[0].ns_translated;
	old->ns_mapped[1] = ns->ns_side[1].ns_translated;
	old->ns_rules = ns->ns_side[0].ns_inuse + ns->ns_side[1].ns_inuse;
	old->ns_added = ns->ns_side[0].ns_added + ns->ns_side[1].ns_added;
	old->ns_expire = ns->ns_expire;
	old->ns_inuse = ns->ns_side[0].ns_inuse + ns->ns_side[1].ns_inuse;
	old->ns_logged = ns->ns_log_ok;
	old->ns_logfail = ns->ns_log_fail;
	old->ns_memfail = ns->ns_side[0].ns_memfail + ns->ns_side[1].ns_memfail;
	old->ns_badnat = ns->ns_side[0].ns_badnat + ns->ns_side[1].ns_badnat;
	old->ns_addtrpnt = ns->ns_addtrpnt;
	old->ns_table[0] = ns->ns_side[0].ns_table;
	old->ns_table[1] = ns->ns_side[1].ns_table;
	old->ns_maptable = NULL;
	old->ns_list = ns->ns_list;
	old->ns_apslist = NULL;
	old->ns_wilds = ns->ns_wilds;
	old->ns_nattab_sz = ns->ns_nattab_sz;
	old->ns_nattab_max = ns->ns_nattab_max;
	old->ns_rultab_sz = ns->ns_rultab_sz;
	old->ns_rdrtab_sz = ns->ns_rdrtab_sz;
	old->ns_trpntab_sz = ns->ns_trpntab_sz;
	old->ns_hostmap_sz = 0;
	old->ns_instances = ns->ns_instances;
	old->ns_maplist = ns->ns_maplist;
	old->ns_bucketlen[0] = (u_long *)ns->ns_side[0].ns_bucketlen;
	old->ns_bucketlen[1] = (u_long *)ns->ns_side[1].ns_bucketlen;
	old->ns_ticks = ns->ns_ticks;
}


static void
natstat_current_to_4_1_0(current, old)
	void *current;
	natstat_4_1_0_t *old;
{
	natstat_t *ns = (natstat_t *)current;

	old->ns_mapped[0] = ns->ns_side[0].ns_translated;
	old->ns_mapped[1] = ns->ns_side[1].ns_translated;
	old->ns_rules = ns->ns_side[0].ns_inuse + ns->ns_side[1].ns_inuse;
	old->ns_added = ns->ns_side[0].ns_added + ns->ns_side[1].ns_added;
	old->ns_expire = ns->ns_expire;
	old->ns_inuse = ns->ns_side[0].ns_inuse + ns->ns_side[1].ns_inuse;
	old->ns_logged = ns->ns_log_ok;
	old->ns_logfail = ns->ns_log_fail;
	old->ns_memfail = ns->ns_side[0].ns_memfail + ns->ns_side[1].ns_memfail;
	old->ns_badnat = ns->ns_side[0].ns_badnat + ns->ns_side[1].ns_badnat;
	old->ns_addtrpnt = ns->ns_addtrpnt;
	old->ns_table[0] = ns->ns_side[0].ns_table;
	old->ns_table[1] = ns->ns_side[1].ns_table;
	old->ns_maptable = NULL;
	old->ns_list = ns->ns_list;
	old->ns_apslist = NULL;
	old->ns_wilds = ns->ns_wilds;
	old->ns_nattab_sz = ns->ns_nattab_sz;
	old->ns_nattab_max = ns->ns_nattab_max;
	old->ns_rultab_sz = ns->ns_rultab_sz;
	old->ns_rdrtab_sz = ns->ns_rdrtab_sz;
	old->ns_trpntab_sz = ns->ns_trpntab_sz;
	old->ns_hostmap_sz = 0;
	old->ns_instances = ns->ns_instances;
	old->ns_maplist = ns->ns_maplist;
	old->ns_bucketlen[0] = (u_long *)ns->ns_side[0].ns_bucketlen;
	old->ns_bucketlen[1] = (u_long *)ns->ns_side[1].ns_bucketlen;
}


static void
ipstate_save_current_to_4_1_16(current, old)
	void *current;
	ipstate_save_4_1_16_t *old;
{
	ipstate_save_t *ips = (ipstate_save_t *)current;

	old->ips_next = ips->ips_next;
	ipstate_current_to_4_1_0(&ips->ips_is, &old->ips_is);
	frentry_current_to_4_1_16(&ips->ips_fr, &old->ips_fr);
}


static void
ipstate_save_current_to_4_1_0(current, old)
	void *current;
	ipstate_save_4_1_0_t *old;
{
	ipstate_save_t *ips = (ipstate_save_t *)current;

	old->ips_next = ips->ips_next;
	ipstate_current_to_4_1_0(&ips->ips_is, &old->ips_is);
	frentry_current_to_4_1_0(&ips->ips_fr, &old->ips_fr);
}


int
ipf_out_compat(softc, obj, ptr)
	ipf_main_softc_t *softc;
	ipfobj_t *obj;
	void *ptr;
{
	frentry_t *fr;
	int error;

	IPFERROR(140042);
	error = EINVAL;

	switch (obj->ipfo_type)
	{
	default :
		break;

	case IPFOBJ_FRENTRY :
		if (obj->ipfo_rev >= 4013400) {
			frentry_4_1_34_t *old;

			KMALLOC(old, frentry_4_1_34_t *);
			if (old == NULL) {
				IPFERROR(140043);
				error = ENOMEM;
				break;
			}
			frentry_current_to_4_1_34(ptr, old);
			error = COPYOUT(old, obj->ipfo_ptr, sizeof(*old));
			if (error == 0 && old->fr_dsize > 0) {
				char *dst = obj->ipfo_ptr;

				fr = ptr;
				dst += sizeof(*old);
				error = COPYOUT(fr->fr_data, dst,
						old->fr_dsize);
				if (error != 0) {
					IPFERROR(140044);
				}
			}
			KFREE(old);
			obj->ipfo_size = sizeof(*old);
		} else if (obj->ipfo_rev >= 4011600) {
			frentry_4_1_16_t *old;

			KMALLOC(old, frentry_4_1_16_t *);
			if (old == NULL) {
				IPFERROR(140045);
				error = ENOMEM;
				break;
			}
			frentry_current_to_4_1_16(ptr, old);
			error = COPYOUT(old, obj->ipfo_ptr, sizeof(*old));
			if (error != 0) {
				IPFERROR(140046);
			}
			KFREE(old);
			obj->ipfo_size = sizeof(*old);
		} else {
			frentry_4_1_0_t *old;

			KMALLOC(old, frentry_4_1_0_t *);
			if (old == NULL) {
				IPFERROR(140047);
				error = ENOMEM;
				break;
			}
			frentry_current_to_4_1_0(ptr, old);
			error = COPYOUT(old, obj->ipfo_ptr, sizeof(*old));
			if (error != 0) {
				IPFERROR(140048);
			}
			KFREE(old);
			obj->ipfo_size = sizeof(*old);
		}
		break;

	case IPFOBJ_IPFSTAT :
		if (obj->ipfo_rev >= 4013300) {
			friostat_4_1_33_t *old;

			KMALLOC(old, friostat_4_1_33_t *);
			if (old == NULL) {
				IPFERROR(140049);
				error = ENOMEM;
				break;
			}
			friostat_current_to_4_1_33(ptr, old, obj->ipfo_rev);
			error = COPYOUT(old, obj->ipfo_ptr, sizeof(*old));
			if (error != 0) {
				IPFERROR(140050);
			}
			KFREE(old);
		} else {
			friostat_4_1_0_t *old;

			KMALLOC(old, friostat_4_1_0_t *);
			if (old == NULL) {
				IPFERROR(140051);
				error = ENOMEM;
				break;
			}
			friostat_current_to_4_1_0(ptr, old, obj->ipfo_rev);
			error = COPYOUT(old, obj->ipfo_ptr, sizeof(*old));
			if (error != 0) {
				IPFERROR(140052);
			}
			KFREE(old);
		}
		break;

	case IPFOBJ_IPFINFO :	/* unused */
		break;

	case IPFOBJ_IPNAT :
		if (obj->ipfo_rev >= 4011400) {
			ipnat_4_1_14_t *old;

			KMALLOC(old, ipnat_4_1_14_t *);
			if (old == NULL) {
				IPFERROR(140053);
				error = ENOMEM;
				break;
			}
			ipnat_current_to_4_1_14(ptr, old);
			error = COPYOUT(old, obj->ipfo_ptr, sizeof(*old));
			if (error != 0) {
				IPFERROR(140054);
			}
			KFREE(old);
		} else {
			ipnat_4_1_0_t *old;

			KMALLOC(old, ipnat_4_1_0_t *);
			if (old == NULL) {
				IPFERROR(140055);
				error = ENOMEM;
				break;
			}
			ipnat_current_to_4_1_0(ptr, old);
			error = COPYOUT(old, obj->ipfo_ptr, sizeof(*old));
			if (error != 0) {
				IPFERROR(140056);
			}
			KFREE(old);
		}
		break;

	case IPFOBJ_NATSTAT :
		if (obj->ipfo_rev >= 4013200) {
			natstat_4_1_32_t *old;

			KMALLOC(old, natstat_4_1_32_t *);
			if (old == NULL) {
				IPFERROR(140057);
				error = ENOMEM;
				break;
			}
			natstat_current_to_4_1_32(ptr, old);
			error = COPYOUT(old, obj->ipfo_ptr, sizeof(*old));
			if (error != 0) {
				IPFERROR(140058);
			}
			KFREE(old);
		} else if (obj->ipfo_rev >= 4012700) {
			natstat_4_1_27_t *old;

			KMALLOC(old, natstat_4_1_27_t *);
			if (old == NULL) {
				IPFERROR(140059);
				error = ENOMEM;
				break;
			}
			natstat_current_to_4_1_27(ptr, old);
			error = COPYOUT(old, obj->ipfo_ptr, sizeof(*old));
			if (error != 0) {
				IPFERROR(140060);
			}
			KFREE(old);
		} else if (obj->ipfo_rev >= 4011600) {
			natstat_4_1_16_t *old;

			KMALLOC(old, natstat_4_1_16_t *);
			if (old == NULL) {
				IPFERROR(140061);
				error = ENOMEM;
				break;
			}
			natstat_current_to_4_1_16(ptr, old);
			error = COPYOUT(old, obj->ipfo_ptr, sizeof(*old));
			if (error != 0) {
				IPFERROR(140062);
			}
			KFREE(old);
		} else {
			natstat_4_1_0_t *old;

			KMALLOC(old, natstat_4_1_0_t *);
			if (old == NULL) {
				IPFERROR(140063);
				error = ENOMEM;
				break;
			}
			natstat_current_to_4_1_0(ptr, old);
			error = COPYOUT(old, obj->ipfo_ptr, sizeof(*old));
			if (error != 0) {
				IPFERROR(140064);
			}
			KFREE(old);
		}
		break;

	case IPFOBJ_STATESAVE :
		if (obj->ipfo_rev >= 4011600) {
			ipstate_save_4_1_16_t *old;

			KMALLOC(old, ipstate_save_4_1_16_t *);
			if (old == NULL) {
				IPFERROR(140065);
				error = ENOMEM;
				break;
			}
			ipstate_save_current_to_4_1_16(ptr, old);
			error = COPYOUT(old, obj->ipfo_ptr, sizeof(*old));
			if (error != 0) {
				IPFERROR(140066);
			}
			KFREE(old);
		} else {
			ipstate_save_4_1_0_t *old;

			KMALLOC(old, ipstate_save_4_1_0_t *);
			if (old == NULL) {
				IPFERROR(140067);
				error = ENOMEM;
				break;
			}
			ipstate_save_current_to_4_1_0(ptr, old);
			error = COPYOUT(old, obj->ipfo_ptr, sizeof(*old));
			if (error != 0) {
				IPFERROR(140068);
			}
			KFREE(old);
		}
		break;

	case IPFOBJ_NATSAVE :
		if (obj->ipfo_rev >= 4011600) {
			nat_save_4_1_16_t *old16;

			KMALLOC(old16, nat_save_4_1_16_t *);
			if (old16 == NULL) {
				IPFERROR(140069);
				error = ENOMEM;
				break;
			}
			nat_save_current_to_4_1_16(ptr, old16);
			error = COPYOUT(&old16, obj->ipfo_ptr, sizeof(*old16));
			if (error != 0) {
				IPFERROR(140070);
			}
			KFREE(old16);
		} else if (obj->ipfo_rev >= 4011400) {
			nat_save_4_1_14_t *old14;

			KMALLOC(old14, nat_save_4_1_14_t *);
			if (old14 == NULL) {
				IPFERROR(140071);
				error = ENOMEM;
				break;
			}
			nat_save_current_to_4_1_14(ptr, old14);
			error = COPYOUT(&old14, obj->ipfo_ptr, sizeof(*old14));
			if (error != 0) {
				IPFERROR(140072);
			}
			KFREE(old14);
		} else if (obj->ipfo_rev >= 4010300) {
			nat_save_4_1_3_t *old3;

			KMALLOC(old3, nat_save_4_1_3_t *);
			if (old3 == NULL) {
				IPFERROR(140073);
				error = ENOMEM;
				break;
			}
			nat_save_current_to_4_1_3(ptr, old3);
			error = COPYOUT(&old3, obj->ipfo_ptr, sizeof(*old3));
			if (error != 0) {
				IPFERROR(140074);
			}
			KFREE(old3);
		}
		break;

	case IPFOBJ_IPSTATE :
		if (obj->ipfo_rev >= 4011600) {
			ipstate_4_1_16_t *old;

			KMALLOC(old, ipstate_4_1_16_t *);
			if (old == NULL) {
				IPFERROR(140075);
				error = ENOMEM;
				break;
			}
			ipstate_current_to_4_1_16(ptr, old);
			error = COPYOUT(old, obj->ipfo_ptr, sizeof(*old));
			if (error != 0) {
				IPFERROR(140076);
			}
			KFREE(old);
		} else {
			ipstate_4_1_0_t *old;

			KMALLOC(old, ipstate_4_1_0_t *);
			if (old == NULL) {
				IPFERROR(140077);
				error = ENOMEM;
				break;
			}
			ipstate_current_to_4_1_0(ptr, old);
			error = COPYOUT(old, obj->ipfo_ptr, sizeof(*old));
			if (error != 0) {
				IPFERROR(140078);
			}
			KFREE(old);
		}
		break;

	case IPFOBJ_STATESTAT :
		if (obj->ipfo_rev >= 4012100) {
			ips_stat_4_1_21_t *old;

			KMALLOC(old, ips_stat_4_1_21_t *);
			if (old == NULL) {
				IPFERROR(140079);
				error = ENOMEM;
				break;
			}
			ips_stat_current_to_4_1_21(ptr, old);
			error = COPYOUT(old, obj->ipfo_ptr, sizeof(*old));
			if (error != 0) {
				IPFERROR(140080);
			}
			KFREE(old);
		} else {
			ips_stat_4_1_0_t *old;

			KMALLOC(old, ips_stat_4_1_0_t *);
			if (old == NULL) {
				IPFERROR(140081);
				error = ENOMEM;
				break;
			}
			ips_stat_current_to_4_1_0(ptr, old);
			error = COPYOUT(old, obj->ipfo_ptr, sizeof(*old));
			if (error != 0) {
				IPFERROR(140082);
			}
			KFREE(old);
		}
		break;

	case IPFOBJ_FRAUTH :
		if (obj->ipfo_rev >= 4012900) {
			frauth_4_1_29_t *old29;

			KMALLOC(old29, frauth_4_1_29_t *);
			if (old29 == NULL) {
				IPFERROR(140083);
				error = ENOMEM;
				break;
			}
			frauth_current_to_4_1_29(ptr, old29);
			error = COPYOUT(old29, obj->ipfo_ptr, sizeof(*old29));
			if (error != 0) {
				IPFERROR(140084);
			}
			KFREE(old29);
		} else if (obj->ipfo_rev >= 4012400) {
			frauth_4_1_24_t *old24;

			KMALLOC(old24, frauth_4_1_24_t *);
			if (old24 == NULL) {
				IPFERROR(140085);
				error = ENOMEM;
				break;
			}
			frauth_current_to_4_1_24(ptr, old24);
			error = COPYOUT(old24, obj->ipfo_ptr, sizeof(*old24));
			if (error != 0) {
				IPFERROR(140086);
			}
			KFREE(old24);
		} else if (obj->ipfo_rev >= 4012300) {
			frauth_4_1_23_t *old23;

			KMALLOC(old23, frauth_4_1_23_t *);
			if (old23 == NULL) {
				IPFERROR(140087);
				error = ENOMEM;
				break;
			}
			frauth_current_to_4_1_23(ptr, old23);
			error = COPYOUT(old23, obj->ipfo_ptr, sizeof(*old23));
			if (error != 0) {
				IPFERROR(140088);
			}
			KFREE(old23);
		} else if (obj->ipfo_rev >= 4011100) {
			frauth_4_1_11_t *old11;

			KMALLOC(old11, frauth_4_1_11_t *);
			if (old11 == NULL) {
				IPFERROR(140089);
				error = ENOMEM;
				break;
			}
			frauth_current_to_4_1_11(ptr, old11);
			error = COPYOUT(old11, obj->ipfo_ptr, sizeof(*old11));
			if (error != 0) {
				IPFERROR(140090);
			}
			KFREE(old11);
		}
		break;

	case IPFOBJ_NAT :
		if (obj->ipfo_rev >= 4012500) {
			nat_4_1_25_t *old;

			KMALLOC(old, nat_4_1_25_t *);
			if (old == NULL) {
				IPFERROR(140091);
				error = ENOMEM;
				break;
			}
			nat_current_to_4_1_25(ptr, old);
			error = COPYOUT(old, obj->ipfo_ptr, sizeof(*old));
			if (error != 0) {
				IPFERROR(140092);
			}
			KFREE(old);
		} else if (obj->ipfo_rev >= 4011400) {
			nat_4_1_14_t *old;

			KMALLOC(old, nat_4_1_14_t *);
			if (old == NULL) {
				IPFERROR(140093);
				error = ENOMEM;
				break;
			}
			nat_current_to_4_1_14(ptr, old);
			error = COPYOUT(old, obj->ipfo_ptr, sizeof(*old));
			if (error != 0) {
				IPFERROR(140094);
			}
			KFREE(old);
		} else if (obj->ipfo_rev >= 4010300) {
			nat_4_1_3_t *old;

			KMALLOC(old, nat_4_1_3_t *);
			if (old == NULL) {
				IPFERROR(140095);
				error = ENOMEM;
				break;
			}
			nat_current_to_4_1_3(ptr, old);
			error = COPYOUT(old, obj->ipfo_ptr, sizeof(*old));
			if (error != 0) {
				IPFERROR(140096);
			}
			KFREE(old);
		}
		break;

	case IPFOBJ_FRIPF :
		if (obj->ipfo_rev < 5000000) {
			fripf4_t *old;

			KMALLOC(old, fripf4_t *);
			if (old == NULL) {
				IPFERROR(140097);
				error = ENOMEM;
				break;
			}
			ipf_v5fripftov4(ptr, old);
			error = COPYOUT(old, obj->ipfo_ptr, sizeof(*old));
			if (error != 0) {
				IPFERROR(140098);
			}
			KFREE(old);
		}
		break;
	}
	return error;
}


static void
friostat_current_to_4_1_33(current, old, rev)
	void *current;
	friostat_4_1_33_t *old;
	int rev;
{
	friostat_t *fiop = (friostat_t *)current;

	bcopy(&fiop->f_st[0].fr_pass, &old->of_st[0], sizeof(old->of_st[0]));
	bcopy(&fiop->f_st[1].fr_pass, &old->of_st[1], sizeof(old->of_st[1]));

	old->f_ipf[0][0] = fiop->f_ipf[0][0];
	old->f_ipf[0][1] = fiop->f_ipf[0][1];
	old->f_ipf[1][0] = fiop->f_ipf[1][0];
	old->f_ipf[1][1] = fiop->f_ipf[1][1];
	old->f_acct[0][0] = fiop->f_acct[0][0];
	old->f_acct[0][1] = fiop->f_acct[0][1];
	old->f_acct[1][0] = fiop->f_acct[1][0];
	old->f_acct[1][1] = fiop->f_acct[1][1];
	old->f_ipf6[0][0] = NULL;
	old->f_ipf6[0][1] = NULL;
	old->f_ipf6[1][0] = NULL;
	old->f_ipf6[1][1] = NULL;
	old->f_acct6[0][0] = NULL;
	old->f_acct6[0][1] = NULL;
	old->f_acct6[1][0] = NULL;
	old->f_acct6[1][1] = NULL;
	old->f_auth = fiop->f_auth;
	bcopy(&fiop->f_groups, &old->f_groups, sizeof(old->f_groups));
	bcopy(&fiop->f_froute, &old->f_froute, sizeof(old->f_froute));
	old->f_ticks = fiop->f_ticks;
	bcopy(&fiop->f_locks, &old->f_locks, sizeof(old->f_locks));
	old->f_kmutex_sz = 0;
	old->f_krwlock_sz = 0;
	old->f_defpass = fiop->f_defpass;
	old->f_active = fiop->f_active;
	old->f_running = fiop->f_running;
	old->f_logging = fiop->f_logging;
	old->f_features = fiop->f_features;
	sprintf(old->f_version, "IP Filter: v%d.%d.%d",
		(rev / 1000000) % 100,
		(rev / 10000) % 100,
		(rev / 100) % 100);
}


static void
friostat_current_to_4_1_0(current, old, rev)
	void *current;
	friostat_4_1_0_t *old;
	int rev;
{
	friostat_t *fiop = (friostat_t *)current;

	bcopy(&fiop->f_st[0].fr_pass, &old->of_st[0], sizeof(old->of_st[0]));
	bcopy(&fiop->f_st[1].fr_pass, &old->of_st[1], sizeof(old->of_st[1]));

	old->f_ipf[0][0] = fiop->f_ipf[0][0];
	old->f_ipf[0][1] = fiop->f_ipf[0][1];
	old->f_ipf[1][0] = fiop->f_ipf[1][0];
	old->f_ipf[1][1] = fiop->f_ipf[1][1];
	old->f_acct[0][0] = fiop->f_acct[0][0];
	old->f_acct[0][1] = fiop->f_acct[0][1];
	old->f_acct[1][0] = fiop->f_acct[1][0];
	old->f_acct[1][1] = fiop->f_acct[1][1];
	old->f_ipf6[0][0] = NULL;
	old->f_ipf6[0][1] = NULL;
	old->f_ipf6[1][0] = NULL;
	old->f_ipf6[1][1] = NULL;
	old->f_acct6[0][0] = NULL;
	old->f_acct6[0][1] = NULL;
	old->f_acct6[1][0] = NULL;
	old->f_acct6[1][1] = NULL;
	old->f_auth = fiop->f_auth;
	bcopy(&fiop->f_groups, &old->f_groups, sizeof(old->f_groups));
	bcopy(&fiop->f_froute, &old->f_froute, sizeof(old->f_froute));
	old->f_ticks = fiop->f_ticks;
	old->f_ipf[0][0] = fiop->f_ipf[0][0];
	old->f_ipf[0][1] = fiop->f_ipf[0][1];
	old->f_ipf[1][0] = fiop->f_ipf[1][0];
	old->f_ipf[1][1] = fiop->f_ipf[1][1];
	old->f_acct[0][0] = fiop->f_acct[0][0];
	old->f_acct[0][1] = fiop->f_acct[0][1];
	old->f_acct[1][0] = fiop->f_acct[1][0];
	old->f_acct[1][1] = fiop->f_acct[1][1];
	old->f_ipf6[0][0] = NULL;
	old->f_ipf6[0][1] = NULL;
	old->f_ipf6[1][0] = NULL;
	old->f_ipf6[1][1] = NULL;
	old->f_acct6[0][0] = NULL;
	old->f_acct6[0][1] = NULL;
	old->f_acct6[1][0] = NULL;
	old->f_acct6[1][1] = NULL;
	old->f_auth = fiop->f_auth;
	bcopy(&fiop->f_groups, &old->f_groups, sizeof(old->f_groups));
	bcopy(&fiop->f_froute, &old->f_froute, sizeof(old->f_froute));
	old->f_ticks = fiop->f_ticks;
	bcopy(&fiop->f_locks, &old->f_locks, sizeof(old->f_locks));
	old->f_kmutex_sz = 0;
	old->f_krwlock_sz = 0;
	old->f_defpass = fiop->f_defpass;
	old->f_active = fiop->f_active;
	old->f_running = fiop->f_running;
	old->f_logging = fiop->f_logging;
	old->f_features = fiop->f_features;
	sprintf(old->f_version, "IP Filter: v%d.%d.%d",
		(rev / 1000000) % 100,
		(rev / 10000) % 100,
		(rev / 100) % 100);
}


/*
 * nflags is v5 flags, returns v4 flags.
 */
static int
fr_frflags5to4(nflags)
	u_32_t nflags;
{
	u_32_t oflags = 0;

	switch (nflags & FR_CMDMASK) {
	case FR_CALL :
		oflags = 0x0;
		break;
	case FR_BLOCK :
		oflags = 0x1;
		break;
	case FR_PASS :
		oflags = 0x2;
		break;
	case FR_AUTH :
		oflags = 0x3;
		break;
	case FR_PREAUTH :
		oflags = 0x4;
		break;
	case FR_ACCOUNT :
		oflags = 0x5;
		break;
	case FR_SKIP :
		oflags = 0x6;
		break;
	default :
		break;
	}

	if (nflags & FR_LOG)
		oflags |= 0x00010;
	if (nflags & FR_CALLNOW)
		oflags |= 0x00020;
	if (nflags & FR_NOTSRCIP)
		oflags |= 0x00080;
	if (nflags & FR_NOTDSTIP)
		oflags |= 0x00040;
	if (nflags & FR_QUICK)
		oflags |= 0x00100;
	if (nflags & FR_KEEPFRAG)
		oflags |= 0x00200;
	if (nflags & FR_KEEPSTATE)
		oflags |= 0x00400;
	if (nflags & FR_FASTROUTE)
		oflags |= 0x00800;
	if (nflags & FR_RETRST)
		oflags |= 0x01000;
	if (nflags & FR_RETICMP)
		oflags |= 0x02000;
	if (nflags & FR_FAKEICMP)
		oflags |= 0x03000;
	if (nflags & FR_OUTQUE)
		oflags |= 0x04000;
	if (nflags & FR_INQUE)
		oflags |= 0x08000;
	if (nflags & FR_LOGBODY)
		oflags |= 0x10000;
	if (nflags & FR_LOGFIRST)
		oflags |= 0x20000;
	if (nflags & FR_LOGORBLOCK)
		oflags |= 0x40000;
	if (nflags & FR_FRSTRICT)
		oflags |= 0x100000;
	if (nflags & FR_STSTRICT)
		oflags |= 0x200000;
	if (nflags & FR_NEWISN)
		oflags |= 0x400000;
	if (nflags & FR_NOICMPERR)
		oflags |= 0x800000;
	if (nflags & FR_STATESYNC)
		oflags |= 0x1000000;
	if (nflags & FR_NOMATCH)
		oflags |= 0x8000000;
	if (nflags & FR_COPIED)
		oflags |= 0x40000000;
	if (nflags & FR_INACTIVE)
		oflags |= 0x80000000;

	return oflags;
}


static void
frentry_current_to_4_1_34(current, old)
	void *current;
	frentry_4_1_34_t *old;
{
	frentry_t *fr = (frentry_t *)current;

	old->fr_lock = fr->fr_lock;
	old->fr_next = fr->fr_next;
	old->fr_grp = (void *)fr->fr_grp;
	old->fr_isc = fr->fr_isc;
	old->fr_ifas[0] = fr->fr_ifas[0];
	old->fr_ifas[1] = fr->fr_ifas[1];
	old->fr_ifas[2] = fr->fr_ifas[2];
	old->fr_ifas[3] = fr->fr_ifas[3];
	old->fr_ptr = fr->fr_ptr;
	old->fr_comment = NULL;
	old->fr_ref = fr->fr_ref;
	old->fr_statecnt = fr->fr_statecnt;
	old->fr_hits = fr->fr_hits;
	old->fr_bytes = fr->fr_bytes;
	old->fr_lastpkt.tv_sec = fr->fr_lastpkt.tv_sec;
	old->fr_lastpkt.tv_usec = fr->fr_lastpkt.tv_usec;
	old->fr_curpps = fr->fr_curpps;
	old->fr_dun.fru_data = fr->fr_dun.fru_data;
	old->fr_func = fr->fr_func;
	old->fr_dsize = fr->fr_dsize;
	old->fr_pps = fr->fr_pps;
	old->fr_statemax = fr->fr_statemax;
	old->fr_flineno = fr->fr_flineno;
	old->fr_type = fr->fr_type;
	old->fr_flags = fr_frflags5to4(fr->fr_flags);
	old->fr_logtag = fr->fr_logtag;
	old->fr_collect = fr->fr_collect;
	old->fr_arg = fr->fr_arg;
	old->fr_loglevel = fr->fr_loglevel;
	old->fr_age[0] = fr->fr_age[0];
	old->fr_age[1] = fr->fr_age[1];
	if (fr->fr_family == AF_INET)
		old->fr_v = 4;
	if (fr->fr_family == AF_INET6)
		old->fr_v = 6;
	old->fr_icode = fr->fr_icode;
	old->fr_cksum = fr->fr_cksum;
	old->fr_tifs[0].ofd_ip6 = fr->fr_tifs[0].fd_ip6;
	old->fr_tifs[1].ofd_ip6 = fr->fr_tifs[0].fd_ip6;
	old->fr_dif.ofd_ip6 = fr->fr_dif.fd_ip6;
	if (fr->fr_ifnames[0] >= 0) {
		strncpy(old->fr_ifnames[0], fr->fr_names + fr->fr_ifnames[0],
			LIFNAMSIZ);
		old->fr_ifnames[0][LIFNAMSIZ - 1] = '\0';
	}
	if (fr->fr_ifnames[1] >= 0) {
		strncpy(old->fr_ifnames[1], fr->fr_names + fr->fr_ifnames[1],
			LIFNAMSIZ);
		old->fr_ifnames[1][LIFNAMSIZ - 1] = '\0';
	}
	if (fr->fr_ifnames[2] >= 0) {
		strncpy(old->fr_ifnames[2], fr->fr_names + fr->fr_ifnames[2],
			LIFNAMSIZ);
		old->fr_ifnames[2][LIFNAMSIZ - 1] = '\0';
	}
	if (fr->fr_ifnames[3] >= 0) {
		strncpy(old->fr_ifnames[3], fr->fr_names + fr->fr_ifnames[3],
			LIFNAMSIZ);
		old->fr_ifnames[3][LIFNAMSIZ - 1] = '\0';
	}
	if (fr->fr_tifs[0].fd_name >= 0) {
		strncpy(old->fr_tifs[0].fd_ifname,
			fr->fr_names + fr->fr_tifs[0].fd_name, LIFNAMSIZ);
		old->fr_tifs[0].fd_ifname[LIFNAMSIZ - 1] = '\0';
	}
	if (fr->fr_tifs[1].fd_name >= 0) {
		strncpy(old->fr_tifs[1].fd_ifname,
			fr->fr_names + fr->fr_tifs[1].fd_name, LIFNAMSIZ);
		old->fr_tifs[1].fd_ifname[LIFNAMSIZ - 1] = '\0';
	}
	if (fr->fr_dif.fd_name >= 0) {
		strncpy(old->fr_dif.fd_ifname,
			fr->fr_names + fr->fr_dif.fd_name, LIFNAMSIZ);
		old->fr_dif.fd_ifname[LIFNAMSIZ - 1] = '\0';
	}
	if (fr->fr_group >= 0) {
		strncpy(old->fr_group, fr->fr_names + fr->fr_group,
			FR_GROUPLEN);
		old->fr_group[FR_GROUPLEN - 1] = '\0';
	}
	if (fr->fr_grhead >= 0) {
		strncpy(old->fr_grhead, fr->fr_names + fr->fr_grhead,
			FR_GROUPLEN);
		old->fr_grhead[FR_GROUPLEN - 1] = '\0';
	}
}


static void
frentry_current_to_4_1_16(current, old)
	void *current;
	frentry_4_1_16_t *old;
{
	frentry_t *fr = (frentry_t *)current;

	old->fr_lock = fr->fr_lock;
	old->fr_next = fr->fr_next;
	old->fr_grp = (void *)fr->fr_grp;
	old->fr_isc = fr->fr_isc;
	old->fr_ifas[0] = fr->fr_ifas[0];
	old->fr_ifas[1] = fr->fr_ifas[1];
	old->fr_ifas[2] = fr->fr_ifas[2];
	old->fr_ifas[3] = fr->fr_ifas[3];
	old->fr_ptr = fr->fr_ptr;
	old->fr_comment = NULL;
	old->fr_ref = fr->fr_ref;
	old->fr_statecnt = fr->fr_statecnt;
	old->fr_hits = fr->fr_hits;
	old->fr_bytes = fr->fr_bytes;
	old->fr_lastpkt.tv_sec = fr->fr_lastpkt.tv_sec;
	old->fr_lastpkt.tv_usec = fr->fr_lastpkt.tv_usec;
	old->fr_curpps = fr->fr_curpps;
	old->fr_dun.fru_data = fr->fr_dun.fru_data;
	old->fr_func = fr->fr_func;
	old->fr_dsize = fr->fr_dsize;
	old->fr_pps = fr->fr_pps;
	old->fr_statemax = fr->fr_statemax;
	old->fr_flineno = fr->fr_flineno;
	old->fr_type = fr->fr_type;
	old->fr_flags = fr_frflags5to4(fr->fr_flags);
	old->fr_logtag = fr->fr_logtag;
	old->fr_collect = fr->fr_collect;
	old->fr_arg = fr->fr_arg;
	old->fr_loglevel = fr->fr_loglevel;
	old->fr_age[0] = fr->fr_age[0];
	old->fr_age[1] = fr->fr_age[1];
	if (old->fr_v == 4)
		fr->fr_family = AF_INET;
	if (old->fr_v == 6)
		fr->fr_family = AF_INET6;
	old->fr_icode = fr->fr_icode;
	old->fr_cksum = fr->fr_cksum;
	old->fr_tifs[0].ofd_ip6 = fr->fr_tifs[0].fd_ip6;
	old->fr_tifs[1].ofd_ip6 = fr->fr_tifs[0].fd_ip6;
	old->fr_dif.ofd_ip6 = fr->fr_dif.fd_ip6;
	if (fr->fr_ifnames[0] >= 0) {
		strncpy(old->fr_ifnames[0], fr->fr_names + fr->fr_ifnames[0],
			LIFNAMSIZ);
		old->fr_ifnames[0][LIFNAMSIZ - 1] = '\0';
	}
	if (fr->fr_ifnames[1] >= 0) {
		strncpy(old->fr_ifnames[1], fr->fr_names + fr->fr_ifnames[1],
			LIFNAMSIZ);
		old->fr_ifnames[1][LIFNAMSIZ - 1] = '\0';
	}
	if (fr->fr_ifnames[2] >= 0) {
		strncpy(old->fr_ifnames[2], fr->fr_names + fr->fr_ifnames[2],
			LIFNAMSIZ);
		old->fr_ifnames[2][LIFNAMSIZ - 1] = '\0';
	}
	if (fr->fr_ifnames[3] >= 0) {
		strncpy(old->fr_ifnames[3], fr->fr_names + fr->fr_ifnames[3],
			LIFNAMSIZ);
		old->fr_ifnames[3][LIFNAMSIZ - 1] = '\0';
	}
	if (fr->fr_tifs[0].fd_name >= 0) {
		strncpy(old->fr_tifs[0].fd_ifname,
			fr->fr_names + fr->fr_tifs[0].fd_name, LIFNAMSIZ);
		old->fr_tifs[0].fd_ifname[LIFNAMSIZ - 1] = '\0';
	}
	if (fr->fr_tifs[1].fd_name >= 0) {
		strncpy(old->fr_tifs[1].fd_ifname,
			fr->fr_names + fr->fr_tifs[1].fd_name, LIFNAMSIZ);
		old->fr_tifs[1].fd_ifname[LIFNAMSIZ - 1] = '\0';
	}
	if (fr->fr_dif.fd_name >= 0) {
		strncpy(old->fr_dif.fd_ifname,
			fr->fr_names + fr->fr_dif.fd_name, LIFNAMSIZ);
		old->fr_dif.fd_ifname[LIFNAMSIZ - 1] = '\0';
	}
	if (fr->fr_group >= 0) {
		strncpy(old->fr_group, fr->fr_names + fr->fr_group,
			FR_GROUPLEN);
		old->fr_group[FR_GROUPLEN - 1] = '\0';
	}
	if (fr->fr_grhead >= 0) {
		strncpy(old->fr_grhead, fr->fr_names + fr->fr_grhead,
			FR_GROUPLEN);
		old->fr_grhead[FR_GROUPLEN - 1] = '\0';
	}
}


static void
frentry_current_to_4_1_0(current, old)
	void *current;
	frentry_4_1_0_t *old;
{
	frentry_t *fr = (frentry_t *)current;

	old->fr_lock = fr->fr_lock;
	old->fr_next = fr->fr_next;
	old->fr_grp = (void *)fr->fr_grp;
	old->fr_isc = fr->fr_isc;
	old->fr_ifas[0] = fr->fr_ifas[0];
	old->fr_ifas[1] = fr->fr_ifas[1];
	old->fr_ifas[2] = fr->fr_ifas[2];
	old->fr_ifas[3] = fr->fr_ifas[3];
	old->fr_ptr = fr->fr_ptr;
	old->fr_comment = NULL;
	old->fr_ref = fr->fr_ref;
	old->fr_statecnt = fr->fr_statecnt;
	old->fr_hits = fr->fr_hits;
	old->fr_bytes = fr->fr_bytes;
	old->fr_lastpkt.tv_sec = fr->fr_lastpkt.tv_sec;
	old->fr_lastpkt.tv_usec = fr->fr_lastpkt.tv_usec;
	old->fr_curpps = fr->fr_curpps;
	old->fr_dun.fru_data = fr->fr_dun.fru_data;
	old->fr_func = fr->fr_func;
	old->fr_dsize = fr->fr_dsize;
	old->fr_pps = fr->fr_pps;
	old->fr_statemax = fr->fr_statemax;
	old->fr_flineno = fr->fr_flineno;
	old->fr_type = fr->fr_type;
	old->fr_flags = fr_frflags5to4(fr->fr_flags);
	old->fr_logtag = fr->fr_logtag;
	old->fr_collect = fr->fr_collect;
	old->fr_arg = fr->fr_arg;
	old->fr_loglevel = fr->fr_loglevel;
	old->fr_age[0] = fr->fr_age[0];
	old->fr_age[1] = fr->fr_age[1];
	if (old->fr_v == 4)
		fr->fr_family = AF_INET;
	if (old->fr_v == 6)
		fr->fr_family = AF_INET6;
	old->fr_icode = fr->fr_icode;
	old->fr_cksum = fr->fr_cksum;
	old->fr_tifs[0].ofd_ip6 = fr->fr_tifs[0].fd_ip6;
	old->fr_tifs[1].ofd_ip6 = fr->fr_tifs[0].fd_ip6;
	old->fr_dif.ofd_ip6 = fr->fr_dif.fd_ip6;
	if (fr->fr_ifnames[0] >= 0) {
		strncpy(old->fr_ifnames[0], fr->fr_names + fr->fr_ifnames[0],
			LIFNAMSIZ);
		old->fr_ifnames[0][LIFNAMSIZ - 1] = '\0';
	}
	if (fr->fr_ifnames[1] >= 0) {
		strncpy(old->fr_ifnames[1], fr->fr_names + fr->fr_ifnames[1],
			LIFNAMSIZ);
		old->fr_ifnames[1][LIFNAMSIZ - 1] = '\0';
	}
	if (fr->fr_ifnames[2] >= 0) {
		strncpy(old->fr_ifnames[2], fr->fr_names + fr->fr_ifnames[2],
			LIFNAMSIZ);
		old->fr_ifnames[2][LIFNAMSIZ - 1] = '\0';
	}
	if (fr->fr_ifnames[3] >= 0) {
		strncpy(old->fr_ifnames[3], fr->fr_names + fr->fr_ifnames[3],
			LIFNAMSIZ);
		old->fr_ifnames[3][LIFNAMSIZ - 1] = '\0';
	}
	if (fr->fr_tifs[0].fd_name >= 0) {
		strncpy(old->fr_tifs[0].fd_ifname,
			fr->fr_names + fr->fr_tifs[0].fd_name, LIFNAMSIZ);
		old->fr_tifs[0].fd_ifname[LIFNAMSIZ - 1] = '\0';
	}
	if (fr->fr_tifs[1].fd_name >= 0) {
		strncpy(old->fr_tifs[1].fd_ifname,
			fr->fr_names + fr->fr_tifs[1].fd_name, LIFNAMSIZ);
		old->fr_tifs[1].fd_ifname[LIFNAMSIZ - 1] = '\0';
	}
	if (fr->fr_dif.fd_name >= 0) {
		strncpy(old->fr_dif.fd_ifname,
			fr->fr_names + fr->fr_dif.fd_name, LIFNAMSIZ);
		old->fr_dif.fd_ifname[LIFNAMSIZ - 1] = '\0';
	}
	if (fr->fr_group >= 0) {
		strncpy(old->fr_group, fr->fr_names + fr->fr_group,
			FR_GROUPLEN);
		old->fr_group[FR_GROUPLEN - 1] = '\0';
	}
	if (fr->fr_grhead >= 0) {
		strncpy(old->fr_grhead, fr->fr_names + fr->fr_grhead,
			FR_GROUPLEN);
		old->fr_grhead[FR_GROUPLEN - 1] = '\0';
	}
}


static void
fr_info_current_to_4_1_24(current, old)
	void *current;
	fr_info_4_1_24_t *old;
{
	fr_info_t *fin = (fr_info_t *)current;

	old->fin_ifp = fin->fin_ifp;
	ipf_v5iptov4(&fin->fin_fi, &old->fin_fi);
	bcopy(&fin->fin_dat, &old->fin_dat, sizeof(fin->fin_dat));
	old->fin_out = fin->fin_out;
	old->fin_rev = fin->fin_rev;
	old->fin_hlen = fin->fin_hlen;
	old->ofin_tcpf = fin->fin_tcpf;
	old->fin_icode = fin->fin_icode;
	old->fin_rule = fin->fin_rule;
	bcopy(fin->fin_group, old->fin_group, sizeof(fin->fin_group));
	old->fin_fr = fin->fin_fr;
	old->fin_dp = fin->fin_dp;
	old->fin_dlen = fin->fin_dlen;
	old->fin_plen = fin->fin_plen;
	old->fin_ipoff = fin->fin_ipoff;
	old->fin_id = fin->fin_id;
	old->fin_off = fin->fin_off;
	old->fin_depth = fin->fin_depth;
	old->fin_error = fin->fin_error;
	old->fin_cksum = fin->fin_cksum;
	old->fin_state = NULL;
	old->fin_nat = NULL;
	old->fin_nattag = fin->fin_nattag;
	old->fin_exthdr = NULL;
	old->ofin_ip = fin->fin_ip;
	old->fin_mp = fin->fin_mp;
	old->fin_m = fin->fin_m;
#ifdef  MENTAT
	old->fin_qfm = fin->fin_qfm;
	old->fin_qpi = fin->fin_qpi;
	old->fin_ifname[0] = '\0';
#endif
}


static void
fr_info_current_to_4_1_23(current, old)
	void *current;
	fr_info_4_1_23_t *old;
{
	fr_info_t *fin = (fr_info_t *)current;

	old->fin_ifp = fin->fin_ifp;
	ipf_v5iptov4(&fin->fin_fi, &old->fin_fi);
	bcopy(&fin->fin_dat, &old->fin_dat, sizeof(fin->fin_dat));
	old->fin_out = fin->fin_out;
	old->fin_rev = fin->fin_rev;
	old->fin_hlen = fin->fin_hlen;
	old->ofin_tcpf = fin->fin_tcpf;
	old->fin_icode = fin->fin_icode;
	old->fin_rule = fin->fin_rule;
	bcopy(fin->fin_group, old->fin_group, sizeof(fin->fin_group));
	old->fin_fr = fin->fin_fr;
	old->fin_dp = fin->fin_dp;
	old->fin_dlen = fin->fin_dlen;
	old->fin_plen = fin->fin_plen;
	old->fin_ipoff = fin->fin_ipoff;
	old->fin_id = fin->fin_id;
	old->fin_off = fin->fin_off;
	old->fin_depth = fin->fin_depth;
	old->fin_error = fin->fin_error;
	old->fin_state = NULL;
	old->fin_nat = NULL;
	old->fin_nattag = fin->fin_nattag;
	old->ofin_ip = fin->fin_ip;
	old->fin_mp = fin->fin_mp;
	old->fin_m = fin->fin_m;
#ifdef  MENTAT
	old->fin_qfm = fin->fin_qfm;
	old->fin_qpi = fin->fin_qpi;
	old->fin_ifname[0] = '\0';
#endif
}


static void
fr_info_current_to_4_1_11(current, old)
	void *current;
	fr_info_4_1_11_t *old;
{
	fr_info_t *fin = (fr_info_t *)current;

	old->fin_ifp = fin->fin_ifp;
	ipf_v5iptov4(&fin->fin_fi, &old->fin_fi);
	bcopy(&fin->fin_dat, &old->fin_dat, sizeof(fin->fin_dat));
	old->fin_out = fin->fin_out;
	old->fin_rev = fin->fin_rev;
	old->fin_hlen = fin->fin_hlen;
	old->ofin_tcpf = fin->fin_tcpf;
	old->fin_icode = fin->fin_icode;
	old->fin_rule = fin->fin_rule;
	bcopy(fin->fin_group, old->fin_group, sizeof(fin->fin_group));
	old->fin_fr = fin->fin_fr;
	old->fin_dp = fin->fin_dp;
	old->fin_dlen = fin->fin_dlen;
	old->fin_plen = fin->fin_plen;
	old->fin_ipoff = fin->fin_ipoff;
	old->fin_id = fin->fin_id;
	old->fin_off = fin->fin_off;
	old->fin_depth = fin->fin_depth;
	old->fin_error = fin->fin_error;
	old->fin_state = NULL;
	old->fin_nat = NULL;
	old->fin_nattag = fin->fin_nattag;
	old->ofin_ip = fin->fin_ip;
	old->fin_mp = fin->fin_mp;
	old->fin_m = fin->fin_m;
#ifdef  MENTAT
	old->fin_qfm = fin->fin_qfm;
	old->fin_qpi = fin->fin_qpi;
	old->fin_ifname[0] = '\0';
#endif
}


static void
frauth_current_to_4_1_29(current, old)
	void *current;
	frauth_4_1_29_t *old;
{
	frauth_t *fra = (frauth_t *)current;

	old->fra_age = fra->fra_age;
	old->fra_len = fra->fra_len;
	old->fra_index = fra->fra_index;
	old->fra_pass = fra->fra_pass;
	fr_info_current_to_4_1_24(&fra->fra_info, &old->fra_info);
	old->fra_buf = fra->fra_buf;
	old->fra_flx = fra->fra_flx;
#ifdef	MENTAT
	old->fra_q = fra->fra_q;
	old->fra_m = fra->fra_m;
#endif
}


static void
frauth_current_to_4_1_24(current, old)
	void *current;
	frauth_4_1_24_t *old;
{
	frauth_t *fra = (frauth_t *)current;

	old->fra_age = fra->fra_age;
	old->fra_len = fra->fra_len;
	old->fra_index = fra->fra_index;
	old->fra_pass = fra->fra_pass;
	fr_info_current_to_4_1_24(&fra->fra_info, &old->fra_info);
	old->fra_buf = fra->fra_buf;
#ifdef	MENTAT
	old->fra_q = fra->fra_q;
	old->fra_m = fra->fra_m;
#endif
}


static void
frauth_current_to_4_1_23(current, old)
	void *current;
	frauth_4_1_23_t *old;
{
	frauth_t *fra = (frauth_t *)current;

	old->fra_age = fra->fra_age;
	old->fra_len = fra->fra_len;
	old->fra_index = fra->fra_index;
	old->fra_pass = fra->fra_pass;
	fr_info_current_to_4_1_23(&fra->fra_info, &old->fra_info);
	old->fra_buf = fra->fra_buf;
#ifdef	MENTAT
	old->fra_q = fra->fra_q;
	old->fra_m = fra->fra_m;
#endif
}


static void
frauth_current_to_4_1_11(current, old)
	void *current;
	frauth_4_1_11_t *old;
{
	frauth_t *fra = (frauth_t *)current;

	old->fra_age = fra->fra_age;
	old->fra_len = fra->fra_len;
	old->fra_index = fra->fra_index;
	old->fra_pass = fra->fra_pass;
	fr_info_current_to_4_1_11(&fra->fra_info, &old->fra_info);
	old->fra_buf = fra->fra_buf;
#ifdef	MENTAT
	old->fra_q = fra->fra_q;
	old->fra_m = fra->fra_m;
#endif
}


static void
ipnat_current_to_4_1_14(current, old)
	void *current;
	ipnat_4_1_14_t *old;
{
	ipnat_t *np = (ipnat_t *)current;

	old->in_next = np->in_next;
	old->in_rnext = np->in_rnext;
	old->in_prnext = np->in_prnext;
	old->in_mnext = np->in_mnext;
	old->in_pmnext = np->in_pmnext;
	old->in_tqehead[0] = np->in_tqehead[0];
	old->in_tqehead[1] = np->in_tqehead[1];
	old->in_ifps[0] = np->in_ifps[0];
	old->in_ifps[1] = np->in_ifps[1];
	old->in_apr = np->in_apr;
	old->in_comment = np->in_comment;
	old->in_space = np->in_space;
	old->in_hits = np->in_hits;
	old->in_use = np->in_use;
	old->in_hv = np->in_hv[0];
	old->in_flineno = np->in_flineno;
	if (old->in_redir == NAT_REDIRECT)
		old->in_pnext = np->in_dpnext;
	else
		old->in_pnext = np->in_spnext;
	old->in_v = np->in_v[0];
	old->in_flags = np->in_flags;
	old->in_mssclamp = np->in_mssclamp;
	old->in_age[0] = np->in_age[0];
	old->in_age[1] = np->in_age[1];
	old->in_redir = np->in_redir;
	old->in_p = np->in_pr[0];
	if (np->in_redir == NAT_REDIRECT) {
		old->in_next6 = np->in_ndst.na_nextaddr;
		old->in_in[0] = np->in_ndst.na_addr[0];
		old->in_in[1] = np->in_ndst.na_addr[1];
		old->in_out[0] = np->in_odst.na_addr[0];
		old->in_out[1] = np->in_odst.na_addr[1];
		old->in_src[0] = np->in_osrc.na_addr[0];
		old->in_src[1] = np->in_osrc.na_addr[1];
	} else {
		old->in_next6 = np->in_nsrc.na_nextaddr;
		old->in_out[0] = np->in_nsrc.na_addr[0];
		old->in_out[1] = np->in_nsrc.na_addr[1];
		old->in_in[0] = np->in_osrc.na_addr[0];
		old->in_in[1] = np->in_osrc.na_addr[1];
		old->in_src[0] = np->in_odst.na_addr[0];
		old->in_src[1] = np->in_odst.na_addr[1];
	}
	ipfv5tuctov4(&np->in_tuc, &old->in_tuc);
	if (np->in_redir == NAT_REDIRECT) {
		old->in_port[0] = np->in_dpmin;
		old->in_port[1] = np->in_dpmax;
	} else {
		old->in_port[0] = np->in_spmin;
		old->in_port[1] = np->in_spmax;
	}
	old->in_ppip = np->in_ppip;
	old->in_ippip = np->in_ippip;
	bcopy(&np->in_tag, &old->in_tag, sizeof(np->in_tag));

	if (np->in_ifnames[0] >= 0) {
		strncpy(old->in_ifnames[0], np->in_names + np->in_ifnames[0],
			LIFNAMSIZ);
		old->in_ifnames[0][LIFNAMSIZ - 1] = '\0';
	}
	if (np->in_ifnames[1] >= 0) {
		strncpy(old->in_ifnames[1], np->in_names + np->in_ifnames[1],
			LIFNAMSIZ);
		old->in_ifnames[1][LIFNAMSIZ - 1] = '\0';
	}
	if (np->in_plabel >= 0) {
		strncpy(old->in_plabel, np->in_names + np->in_plabel,
			APR_LABELLEN);
		old->in_plabel[APR_LABELLEN - 1] = '\0';
	}
}


static void
ipnat_current_to_4_1_0(current, old)
	void *current;
	ipnat_4_1_0_t *old;
{
	ipnat_t *np = (ipnat_t *)current;

	old->in_next = np->in_next;
	old->in_rnext = np->in_rnext;
	old->in_prnext = np->in_prnext;
	old->in_mnext = np->in_mnext;
	old->in_pmnext = np->in_pmnext;
	old->in_tqehead[0] = np->in_tqehead[0];
	old->in_tqehead[1] = np->in_tqehead[1];
	old->in_ifps[0] = np->in_ifps[0];
	old->in_ifps[1] = np->in_ifps[1];
	old->in_apr = np->in_apr;
	old->in_comment = np->in_comment;
	old->in_space = np->in_space;
	old->in_hits = np->in_hits;
	old->in_use = np->in_use;
	old->in_hv = np->in_hv[0];
	old->in_flineno = np->in_flineno;
	if (old->in_redir == NAT_REDIRECT)
		old->in_pnext = np->in_dpnext;
	else
		old->in_pnext = np->in_spnext;
	old->in_v = np->in_v[0];
	old->in_flags = np->in_flags;
	old->in_mssclamp = np->in_mssclamp;
	old->in_age[0] = np->in_age[0];
	old->in_age[1] = np->in_age[1];
	old->in_redir = np->in_redir;
	old->in_p = np->in_pr[0];
	if (np->in_redir == NAT_REDIRECT) {
		old->in_next6 = np->in_ndst.na_nextaddr;
		old->in_in[0] = np->in_ndst.na_addr[0];
		old->in_in[1] = np->in_ndst.na_addr[1];
		old->in_out[0] = np->in_odst.na_addr[0];
		old->in_out[1] = np->in_odst.na_addr[1];
		old->in_src[0] = np->in_osrc.na_addr[0];
		old->in_src[1] = np->in_osrc.na_addr[1];
	} else {
		old->in_next6 = np->in_nsrc.na_nextaddr;
		old->in_out[0] = np->in_nsrc.na_addr[0];
		old->in_out[1] = np->in_nsrc.na_addr[1];
		old->in_in[0] = np->in_osrc.na_addr[0];
		old->in_in[1] = np->in_osrc.na_addr[1];
		old->in_src[0] = np->in_odst.na_addr[0];
		old->in_src[1] = np->in_odst.na_addr[1];
	}
	ipfv5tuctov4(&np->in_tuc, &old->in_tuc);
	if (np->in_redir == NAT_REDIRECT) {
		old->in_port[0] = np->in_dpmin;
		old->in_port[1] = np->in_dpmax;
	} else {
		old->in_port[0] = np->in_spmin;
		old->in_port[1] = np->in_spmax;
	}
	old->in_ppip = np->in_ppip;
	old->in_ippip = np->in_ippip;
	bcopy(&np->in_tag, &old->in_tag, sizeof(np->in_tag));

	if (np->in_ifnames[0] >= 0) {
		strncpy(old->in_ifnames[0], np->in_names + np->in_ifnames[0],
			LIFNAMSIZ);
		old->in_ifnames[0][LIFNAMSIZ - 1] = '\0';
	}
	if (np->in_ifnames[1] >= 0) {
		strncpy(old->in_ifnames[1], np->in_names + np->in_ifnames[1],
			LIFNAMSIZ);
		old->in_ifnames[1][LIFNAMSIZ - 1] = '\0';
	}
	if (np->in_plabel >= 0) {
		strncpy(old->in_plabel, np->in_names + np->in_plabel,
			APR_LABELLEN);
		old->in_plabel[APR_LABELLEN - 1] = '\0';
	}
}


static void
ipstate_current_to_4_1_16(current, old)
	void *current;
	ipstate_4_1_16_t *old;
{
	ipstate_t *is = (ipstate_t *)current;

	old->is_lock = is->is_lock;
	old->is_next = is->is_next;
	old->is_pnext = is->is_pnext;
	old->is_hnext = is->is_hnext;
	old->is_phnext = is->is_phnext;
	old->is_me = is->is_me;
	old->is_ifp[0] = is->is_ifp[0];
	old->is_ifp[1] = is->is_ifp[1];
	old->is_sync = is->is_sync;
	old->is_rule = is->is_rule;
	old->is_tqehead[0] = is->is_tqehead[0];
	old->is_tqehead[1] = is->is_tqehead[1];
	old->is_isc = is->is_isc;
	old->is_pkts[0] = is->is_pkts[0];
	old->is_pkts[1] = is->is_pkts[1];
	old->is_pkts[2] = is->is_pkts[2];
	old->is_pkts[3] = is->is_pkts[3];
	old->is_bytes[0] = is->is_bytes[0];
	old->is_bytes[1] = is->is_bytes[1];
	old->is_bytes[2] = is->is_bytes[2];
	old->is_bytes[3] = is->is_bytes[3];
	old->is_icmppkts[0] = is->is_icmppkts[0];
	old->is_icmppkts[1] = is->is_icmppkts[1];
	old->is_icmppkts[2] = is->is_icmppkts[2];
	old->is_icmppkts[3] = is->is_icmppkts[3];
	old->is_sti = is->is_sti;
	old->is_frage[0] = is->is_frage[0];
	old->is_frage[1] = is->is_frage[1];
	old->is_ref = is->is_ref;
	old->is_isninc[0] = is->is_isninc[0];
	old->is_isninc[1] = is->is_isninc[1];
	old->is_sumd[0] = is->is_sumd[0];
	old->is_sumd[1] = is->is_sumd[1];
	old->is_src = is->is_src;
	old->is_dst = is->is_dst;
	old->is_pass = is->is_pass;
	old->is_p = is->is_p;
	old->is_v = is->is_v;
	old->is_hv = is->is_hv;
	old->is_tag = is->is_tag;
	old->is_opt[0] = is->is_opt[0];
	old->is_opt[1] = is->is_opt[1];
	old->is_optmsk[0] = is->is_optmsk[0];
	old->is_optmsk[1] = is->is_optmsk[1];
	old->is_sec = is->is_sec;
	old->is_secmsk = is->is_secmsk;
	old->is_auth = is->is_auth;
	old->is_authmsk = is->is_authmsk;
	ipf_v5tcpinfoto4(&is->is_tcp, &old->is_tcp);
	old->is_flags = is->is_flags;
	old->is_flx[0][0] = is->is_flx[0][0];
	old->is_flx[0][1] = is->is_flx[0][1];
	old->is_flx[1][0] = is->is_flx[1][0];
	old->is_flx[1][1] = is->is_flx[1][1];
	old->is_rulen = is->is_rulen;
	old->is_s0[0] = is->is_s0[0];
	old->is_s0[1] = is->is_s0[1];
	old->is_smsk[0] = is->is_smsk[0];
	old->is_smsk[1] = is->is_smsk[1];
	bcopy(is->is_group, old->is_group, sizeof(is->is_group));
	bcopy(is->is_sbuf, old->is_sbuf, sizeof(is->is_sbuf));
	bcopy(is->is_ifname, old->is_ifname, sizeof(is->is_ifname));
}


static void
ipstate_current_to_4_1_0(current, old)
	void *current;
	ipstate_4_1_0_t *old;
{
	ipstate_t *is = (ipstate_t *)current;

	old->is_lock = is->is_lock;
	old->is_next = is->is_next;
	old->is_pnext = is->is_pnext;
	old->is_hnext = is->is_hnext;
	old->is_phnext = is->is_phnext;
	old->is_me = is->is_me;
	old->is_ifp[0] = is->is_ifp[0];
	old->is_ifp[1] = is->is_ifp[1];
	old->is_sync = is->is_sync;
	bzero(&old->is_nat, sizeof(old->is_nat));
	old->is_rule = is->is_rule;
	old->is_tqehead[0] = is->is_tqehead[0];
	old->is_tqehead[1] = is->is_tqehead[1];
	old->is_isc = is->is_isc;
	old->is_pkts[0] = is->is_pkts[0];
	old->is_pkts[1] = is->is_pkts[1];
	old->is_pkts[2] = is->is_pkts[2];
	old->is_pkts[3] = is->is_pkts[3];
	old->is_bytes[0] = is->is_bytes[0];
	old->is_bytes[1] = is->is_bytes[1];
	old->is_bytes[2] = is->is_bytes[2];
	old->is_bytes[3] = is->is_bytes[3];
	old->is_icmppkts[0] = is->is_icmppkts[0];
	old->is_icmppkts[1] = is->is_icmppkts[1];
	old->is_icmppkts[2] = is->is_icmppkts[2];
	old->is_icmppkts[3] = is->is_icmppkts[3];
	old->is_sti = is->is_sti;
	old->is_frage[0] = is->is_frage[0];
	old->is_frage[1] = is->is_frage[1];
	old->is_ref = is->is_ref;
	old->is_isninc[0] = is->is_isninc[0];
	old->is_isninc[1] = is->is_isninc[1];
	old->is_sumd[0] = is->is_sumd[0];
	old->is_sumd[1] = is->is_sumd[1];
	old->is_src = is->is_src;
	old->is_dst = is->is_dst;
	old->is_pass = is->is_pass;
	old->is_p = is->is_p;
	old->is_v = is->is_v;
	old->is_hv = is->is_hv;
	old->is_tag = is->is_tag;
	old->is_opt[0] = is->is_opt[0];
	old->is_opt[1] = is->is_opt[1];
	old->is_optmsk[0] = is->is_optmsk[0];
	old->is_optmsk[1] = is->is_optmsk[1];
	old->is_sec = is->is_sec;
	old->is_secmsk = is->is_secmsk;
	old->is_auth = is->is_auth;
	old->is_authmsk = is->is_authmsk;
	ipf_v5tcpinfoto4(&is->is_tcp, &old->is_tcp);
	old->is_flags = is->is_flags;
	old->is_flx[0][0] = is->is_flx[0][0];
	old->is_flx[0][1] = is->is_flx[0][1];
	old->is_flx[1][0] = is->is_flx[1][0];
	old->is_flx[1][1] = is->is_flx[1][1];
	old->is_rulen = is->is_rulen;
	old->is_s0[0] = is->is_s0[0];
	old->is_s0[1] = is->is_s0[1];
	old->is_smsk[0] = is->is_smsk[0];
	old->is_smsk[1] = is->is_smsk[1];
	bcopy(is->is_group, old->is_group, sizeof(is->is_group));
	bcopy(is->is_sbuf, old->is_sbuf, sizeof(is->is_sbuf));
	bcopy(is->is_ifname, old->is_ifname, sizeof(is->is_ifname));
}


static void
ips_stat_current_to_4_1_21(current, old)
	void *current;
	ips_stat_4_1_21_t *old;
{
	ips_stat_t *st = (ips_stat_t *)current;

	old->iss_hits = st->iss_hits;
	old->iss_miss = st->iss_check_miss;
	old->iss_max = st->iss_max;
	old->iss_maxref = st->iss_max_ref;
	old->iss_tcp = st->iss_proto[IPPROTO_TCP];
	old->iss_udp = st->iss_proto[IPPROTO_UDP];
	old->iss_icmp = st->iss_proto[IPPROTO_ICMP];
	old->iss_nomem = st->iss_nomem;
	old->iss_expire = st->iss_expire;
	old->iss_fin = st->iss_fin;
	old->iss_active = st->iss_active;
	old->iss_logged = st->iss_log_ok;
	old->iss_logfail = st->iss_log_fail;
	old->iss_inuse = st->iss_inuse;
	old->iss_wild = st->iss_wild;
	old->iss_ticks = st->iss_ticks;
	old->iss_bucketfull = st->iss_bucket_full;
	old->iss_statesize = st->iss_state_size;
	old->iss_statemax = st->iss_state_max;
	old->iss_table = st->iss_table;
	old->iss_list = st->iss_list;
	old->iss_bucketlen = (void *)st->iss_bucketlen;
	old->iss_tcptab = st->iss_tcptab;
}


static void
ips_stat_current_to_4_1_0(current, old)
	void *current;
	ips_stat_4_1_0_t *old;
{
	ips_stat_t *st = (ips_stat_t *)current;

	old->iss_hits = st->iss_hits;
	old->iss_miss = st->iss_check_miss;
	old->iss_max = st->iss_max;
	old->iss_maxref = st->iss_max_ref;
	old->iss_tcp = st->iss_proto[IPPROTO_TCP];
	old->iss_udp = st->iss_proto[IPPROTO_UDP];
	old->iss_icmp = st->iss_proto[IPPROTO_ICMP];
	old->iss_nomem = st->iss_nomem;
	old->iss_expire = st->iss_expire;
	old->iss_fin = st->iss_fin;
	old->iss_active = st->iss_active;
	old->iss_logged = st->iss_log_ok;
	old->iss_logfail = st->iss_log_fail;
	old->iss_inuse = st->iss_inuse;
	old->iss_wild = st->iss_wild;
	old->iss_ticks = st->iss_ticks;
	old->iss_bucketfull = st->iss_bucket_full;
	old->iss_statesize = st->iss_state_size;
	old->iss_statemax = st->iss_state_max;
	old->iss_table = st->iss_table;
	old->iss_list = st->iss_list;
	old->iss_bucketlen = (void *)st->iss_bucketlen;
}


static void
nat_save_current_to_4_1_16(current, old)
	void *current;
	nat_save_4_1_16_t *old;
{
	nat_save_t *nats = (nat_save_t *)current;

	old->ipn_next = nats->ipn_next;
	bcopy(&nats->ipn_nat, &old->ipn_nat, sizeof(old->ipn_nat));
	bcopy(&nats->ipn_ipnat, &old->ipn_ipnat, sizeof(old->ipn_ipnat));
	frentry_current_to_4_1_16(&nats->ipn_fr, &old->ipn_fr);
	old->ipn_dsize = nats->ipn_dsize;
	bcopy(nats->ipn_data, old->ipn_data, sizeof(nats->ipn_data));
}


static void
nat_save_current_to_4_1_14(current, old)
	void *current;
	nat_save_4_1_14_t *old;
{
	nat_save_t *nats = (nat_save_t *)current;

	old->ipn_next = nats->ipn_next;
	bcopy(&nats->ipn_nat, &old->ipn_nat, sizeof(old->ipn_nat));
	bcopy(&nats->ipn_ipnat, &old->ipn_ipnat, sizeof(old->ipn_ipnat));
	frentry_current_to_4_1_0(&nats->ipn_fr, &old->ipn_fr);
	old->ipn_dsize = nats->ipn_dsize;
	bcopy(nats->ipn_data, old->ipn_data, sizeof(nats->ipn_data));
}


static void
nat_save_current_to_4_1_3(current, old)
	void *current;
	nat_save_4_1_3_t *old;
{
	nat_save_t *nats = (nat_save_t *)current;

	old->ipn_next = nats->ipn_next;
	bcopy(&nats->ipn_nat, &old->ipn_nat, sizeof(old->ipn_nat));
	bcopy(&nats->ipn_ipnat, &old->ipn_ipnat, sizeof(old->ipn_ipnat));
	frentry_current_to_4_1_0(&nats->ipn_fr, &old->ipn_fr);
	old->ipn_dsize = nats->ipn_dsize;
	bcopy(nats->ipn_data, old->ipn_data, sizeof(nats->ipn_data));
}


static void
nat_current_to_4_1_25(current, old)
	void *current;
	nat_4_1_25_t *old;
{
	nat_t *nat = (nat_t *)current;

	old->nat_lock = nat->nat_lock;
	old->nat_next = (void *)nat->nat_next;
	old->nat_pnext = (void *)nat->nat_pnext;
	old->nat_hnext[0] = (void *)nat->nat_hnext[0];
	old->nat_hnext[1] = (void *)nat->nat_hnext[1];
	old->nat_phnext[0] = (void *)nat->nat_phnext[0];
	old->nat_phnext[1] = (void *)nat->nat_phnext[1];
	old->nat_hm = nat->nat_hm;
	old->nat_data = nat->nat_data;
	old->nat_me = (void *)nat->nat_me;
	old->nat_state = nat->nat_state;
	old->nat_aps = nat->nat_aps;
	old->nat_fr = nat->nat_fr;
	old->nat_ptr = (void *)nat->nat_ptr;
	old->nat_ifps[0] = nat->nat_ifps[0];
	old->nat_ifps[1] = nat->nat_ifps[1];
	old->nat_sync = nat->nat_sync;
	old->nat_tqe = nat->nat_tqe;
	old->nat_flags = nat->nat_flags;
	old->nat_sumd[0] = nat->nat_sumd[0];
	old->nat_sumd[1] = nat->nat_sumd[1];
	old->nat_ipsumd = nat->nat_ipsumd;
	old->nat_mssclamp = nat->nat_mssclamp;
	old->nat_pkts[0] = nat->nat_pkts[0];
	old->nat_pkts[1] = nat->nat_pkts[1];
	old->nat_bytes[0] = nat->nat_bytes[0];
	old->nat_bytes[1] = nat->nat_bytes[1];
	old->nat_ref = nat->nat_ref;
	old->nat_dir = nat->nat_dir;
	old->nat_p = nat->nat_pr[0];
	old->nat_use = nat->nat_use;
	old->nat_hv[0] = nat->nat_hv[0];
	old->nat_hv[1] = nat->nat_hv[1];
	old->nat_rev = nat->nat_rev;
	old->nat_redir = nat->nat_redir;
	bcopy(nat->nat_ifnames[0], old->nat_ifnames[0], LIFNAMSIZ);
	bcopy(nat->nat_ifnames[1], old->nat_ifnames[1], LIFNAMSIZ);

	if (nat->nat_redir == NAT_REDIRECT) {
		old->nat_inip6 = nat->nat_ndst6;
		old->nat_outip6 = nat->nat_odst6;
		old->nat_oip6 = nat->nat_osrc6;
		old->nat_un.nat_unt.ts_sport = nat->nat_ndport;
		old->nat_un.nat_unt.ts_dport = nat->nat_odport;
	} else {
		old->nat_inip6 = nat->nat_osrc6;
		old->nat_outip6 = nat->nat_nsrc6;
		old->nat_oip6 = nat->nat_odst6;
		old->nat_un.nat_unt.ts_sport = nat->nat_osport;
		old->nat_un.nat_unt.ts_dport = nat->nat_nsport;
	}
}


static void
nat_current_to_4_1_14(current, old)
	void *current;
	nat_4_1_14_t *old;
{
	nat_t *nat = (nat_t *)current;

	old->nat_lock = nat->nat_lock;
	old->nat_next = nat->nat_next;
	old->nat_pnext = NULL;
	old->nat_hnext[0] = NULL;
	old->nat_hnext[1] = NULL;
	old->nat_phnext[0] = NULL;
	old->nat_phnext[1] = NULL;
	old->nat_hm = nat->nat_hm;
	old->nat_data = nat->nat_data;
	old->nat_me = (void *)nat->nat_me;
	old->nat_state = nat->nat_state;
	old->nat_aps = nat->nat_aps;
	old->nat_fr = nat->nat_fr;
	old->nat_ptr = nat->nat_ptr;
	old->nat_ifps[0] = nat->nat_ifps[0];
	old->nat_ifps[1] = nat->nat_ifps[1];
	old->nat_sync = nat->nat_sync;
	old->nat_tqe = nat->nat_tqe;
	old->nat_flags = nat->nat_flags;
	old->nat_sumd[0] = nat->nat_sumd[0];
	old->nat_sumd[1] = nat->nat_sumd[1];
	old->nat_ipsumd = nat->nat_ipsumd;
	old->nat_mssclamp = nat->nat_mssclamp;
	old->nat_pkts[0] = nat->nat_pkts[0];
	old->nat_pkts[1] = nat->nat_pkts[1];
	old->nat_bytes[0] = nat->nat_bytes[0];
	old->nat_bytes[1] = nat->nat_bytes[1];
	old->nat_ref = nat->nat_ref;
	old->nat_dir = nat->nat_dir;
	old->nat_p = nat->nat_pr[0];
	old->nat_use = nat->nat_use;
	old->nat_hv[0] = nat->nat_hv[0];
	old->nat_hv[1] = nat->nat_hv[1];
	old->nat_rev = nat->nat_rev;
	bcopy(nat->nat_ifnames[0], old->nat_ifnames[0], LIFNAMSIZ);
	bcopy(nat->nat_ifnames[1], old->nat_ifnames[1], LIFNAMSIZ);

	if (nat->nat_redir == NAT_REDIRECT) {
		old->nat_inip6 = nat->nat_ndst6;
		old->nat_outip6 = nat->nat_odst6;
		old->nat_oip6 = nat->nat_osrc6;
		old->nat_un.nat_unt.ts_sport = nat->nat_ndport;
		old->nat_un.nat_unt.ts_dport = nat->nat_odport;
	} else {
		old->nat_inip6 = nat->nat_osrc6;
		old->nat_outip6 = nat->nat_nsrc6;
		old->nat_oip6 = nat->nat_odst6;
		old->nat_un.nat_unt.ts_sport = nat->nat_osport;
		old->nat_un.nat_unt.ts_dport = nat->nat_nsport;
	}
}


static void
nat_current_to_4_1_3(current, old)
	void *current;
	nat_4_1_3_t *old;
{
	nat_t *nat = (nat_t *)current;

	old->nat_lock = nat->nat_lock;
	old->nat_next = nat->nat_next;
	old->nat_pnext = NULL;
	old->nat_hnext[0] = NULL;
	old->nat_hnext[1] = NULL;
	old->nat_phnext[0] = NULL;
	old->nat_phnext[1] = NULL;
	old->nat_hm = nat->nat_hm;
	old->nat_data = nat->nat_data;
	old->nat_me = (void *)nat->nat_me;
	old->nat_state = nat->nat_state;
	old->nat_aps = nat->nat_aps;
	old->nat_fr = nat->nat_fr;
	old->nat_ptr = nat->nat_ptr;
	old->nat_ifps[0] = nat->nat_ifps[0];
	old->nat_ifps[1] = nat->nat_ifps[1];
	old->nat_sync = nat->nat_sync;
	old->nat_tqe = nat->nat_tqe;
	old->nat_flags = nat->nat_flags;
	old->nat_sumd[0] = nat->nat_sumd[0];
	old->nat_sumd[1] = nat->nat_sumd[1];
	old->nat_ipsumd = nat->nat_ipsumd;
	old->nat_mssclamp = nat->nat_mssclamp;
	old->nat_pkts[0] = nat->nat_pkts[0];
	old->nat_pkts[1] = nat->nat_pkts[1];
	old->nat_bytes[0] = nat->nat_bytes[0];
	old->nat_bytes[1] = nat->nat_bytes[1];
	old->nat_ref = nat->nat_ref;
	old->nat_dir = nat->nat_dir;
	old->nat_p = nat->nat_pr[0];
	old->nat_use = nat->nat_use;
	old->nat_hv[0] = nat->nat_hv[0];
	old->nat_hv[1] = nat->nat_hv[1];
	old->nat_rev = nat->nat_rev;
	bcopy(nat->nat_ifnames[0], old->nat_ifnames[0], LIFNAMSIZ);
	bcopy(nat->nat_ifnames[1], old->nat_ifnames[1], LIFNAMSIZ);

	if (nat->nat_redir == NAT_REDIRECT) {
		old->nat_inip6 = nat->nat_ndst6;
		old->nat_outip6 = nat->nat_odst6;
		old->nat_oip6 = nat->nat_osrc6;
		old->nat_un.nat_unt.ts_sport = nat->nat_ndport;
		old->nat_un.nat_unt.ts_dport = nat->nat_odport;
	} else {
		old->nat_inip6 = nat->nat_osrc6;
		old->nat_outip6 = nat->nat_nsrc6;
		old->nat_oip6 = nat->nat_odst6;
		old->nat_un.nat_unt.ts_sport = nat->nat_osport;
		old->nat_un.nat_unt.ts_dport = nat->nat_nsport;
	}
}

#endif /* IPFILTER_COMPAT */
