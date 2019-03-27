/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * @(#)ipf.h	1.12 6/5/96
 * $Id$
 */

#ifndef	__IPF_H__
#define	__IPF_H__


#include <sys/param.h>
#include <sys/types.h>
#include <sys/file.h>
/*
 * This is a workaround for <sys/uio.h> troubles on FreeBSD, HPUX, OpenBSD.
 * Needed here because on some systems <sys/uio.h> gets included by things
 * like <sys/socket.h>
 */
#ifndef _KERNEL
# define ADD_KERNEL
# define _KERNEL
# define KERNEL
#endif
#include <sys/uio.h>
#ifdef ADD_KERNEL
# undef _KERNEL
# undef KERNEL
#endif
#include <sys/time.h>
#include <sys/socket.h>
#include <net/if.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
# include <netinet/tcp.h>
#include <netinet/udp.h>

#include <arpa/inet.h>

#include <errno.h>
#include <limits.h>
#include <netdb.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#if !defined(__SVR4) && !defined(__svr4__) && defined(sun)
# include <strings.h>
#endif
#include <string.h>
#include <unistd.h>

#include "netinet/ip_compat.h"
#include "netinet/ip_fil.h"
#include "netinet/ip_nat.h"
#include "netinet/ip_frag.h"
#include "netinet/ip_state.h"
#include "netinet/ip_proxy.h"
#include "netinet/ip_auth.h"
#include "netinet/ip_lookup.h"
#include "netinet/ip_pool.h"
#include "netinet/ip_scan.h"
#include "netinet/ip_htable.h"
#include "netinet/ip_sync.h"
#include "netinet/ip_dstlist.h"

#include "opts.h"

#ifndef __P
# ifdef __STDC__
#  define	__P(x)	x
# else
#  define	__P(x)	()
# endif
#endif
#ifndef __STDC__
# undef		const
# define	const
#endif

#ifndef	U_32_T
# define	U_32_T	1
# if defined(__NetBSD__) || defined(__OpenBSD__) || defined(__FreeBSD__) || \
    defined(__sgi)
typedef	u_int32_t	u_32_t;
# else
#  if defined(__alpha__) || defined(__alpha) || defined(_LP64)
typedef unsigned int	u_32_t;
#  else
#   if SOLARIS2 >= 6
typedef uint32_t	u_32_t;
#   else
typedef unsigned int	u_32_t;
#   endif
#  endif
# endif /* __NetBSD__ || __OpenBSD__ || __FreeBSD__ || __sgi */
#endif /* U_32_T */

#ifndef	MAXHOSTNAMELEN
# define	MAXHOSTNAMELEN	256
#endif

#define	MAX_ICMPCODE	16
#define	MAX_ICMPTYPE	19

#define	PRINTF	(void)printf
#define	FPRINTF	(void)fprintf


struct	ipopt_names	{
	int	on_value;
	int	on_bit;
	int	on_siz;
	char	*on_name;
};


typedef struct  alist_s {
	struct	alist_s	*al_next;
	int		al_not;
	int		al_family;
	i6addr_t	al_i6addr;
	i6addr_t	al_i6mask;
} alist_t;

#define	al_addr	al_i6addr.in4_addr
#define	al_mask	al_i6mask.in4_addr
#define	al_1	al_addr
#define	al_2	al_mask


typedef struct  plist_s {
	struct	plist_s	*pl_next;
	int		pl_compare;
	u_short		pl_port1;
	u_short		pl_port2;
} plist_t;


typedef	struct	{
	u_short	fb_c;
	u_char	fb_t;
	u_char	fb_f;
	u_32_t	fb_k;
} fakebpf_t;


typedef struct  {
	char	*it_name;
	int	it_v4;
	int	it_v6;
} icmptype_t;


typedef	struct	wordtab {
	char	*w_word;
	int	w_value;
} wordtab_t;


typedef	struct	namelist {
	struct namelist	*na_next;
	char		*na_name;
	int		na_value;
} namelist_t;


typedef	struct	proxyrule {
	struct	proxyrule	*pr_next;
	char			*pr_proxy;
	char			*pr_conf;
	namelist_t		*pr_names;
	int			pr_proto;
} proxyrule_t;


#if defined(__NetBSD__) || defined(__FreeBSD_version) || \
	SOLARIS
# include <stdarg.h>
typedef	int	(* ioctlfunc_t) __P((int, ioctlcmd_t, ...));
#else
typedef	int	(* ioctlfunc_t) __P((dev_t, ioctlcmd_t, void *));
#endif
typedef	int	(* addfunc_t) __P((int, ioctlfunc_t, void *));
typedef	int	(* copyfunc_t) __P((void *, void *, size_t));


extern	char	thishost[];
extern	char	flagset[];
extern	u_char	flags[];
extern	struct ipopt_names ionames[];
extern	struct ipopt_names secclass[];
extern	char	*icmpcodes[MAX_ICMPCODE + 1];
extern	char	*icmptypes[MAX_ICMPTYPE + 1];
extern	int	use_inet6;
extern	int	lineNum;
extern	int	debuglevel;
extern	struct ipopt_names v6ionames[];
extern	icmptype_t icmptypelist[];
extern	wordtab_t statefields[];
extern	wordtab_t natfields[];
extern	wordtab_t poolfields[];


extern int addicmp __P((char ***, struct frentry *, int));
extern int addipopt __P((char *, struct ipopt_names *, int, char *));
extern int addkeep __P((char ***, struct frentry *, int));
extern alist_t *alist_new __P((int, char *));
extern void alist_free __P((alist_t *));
extern void assigndefined __P((char *));
extern void binprint __P((void *, size_t));
extern u_32_t buildopts __P((char *, char *, int));
extern int checkrev __P((char *));
extern int connecttcp __P((char *, int));
extern int count6bits __P((u_32_t *));
extern int count4bits __P((u_32_t));
extern char *fac_toname __P((int));
extern int fac_findname __P((char *));
extern const char *familyname __P((const int));
extern void fill6bits __P((int, u_int *));
extern wordtab_t *findword __P((wordtab_t *, char *));
extern int ftov __P((int));
extern char *ipf_geterror __P((int, ioctlfunc_t *));
extern int genmask __P((int, char *, i6addr_t *));
extern int gethost __P((int, char *, i6addr_t *));
extern int geticmptype __P((int, char *));
extern int getport __P((struct frentry *, char *, u_short *, char *));
extern int getportproto __P((char *, int));
extern int getproto __P((char *));
extern char *getnattype __P((struct nat *));
extern char *getsumd __P((u_32_t));
extern u_32_t getoptbyname __P((char *));
extern u_32_t getoptbyvalue __P((int));
extern u_32_t getv6optbyname __P((char *));
extern u_32_t getv6optbyvalue __P((int));
extern char *icmptypename __P((int, int));
extern void initparse __P((void));
extern void ipf_dotuning __P((int, char *, ioctlfunc_t));
extern int ipf_addrule __P((int, ioctlfunc_t, void *));
extern void ipf_mutex_clean __P((void));
extern int ipf_parsefile __P((int, addfunc_t, ioctlfunc_t *, char *));
extern int ipf_parsesome __P((int, addfunc_t, ioctlfunc_t *, FILE *));
extern void ipf_perror __P((int, char *));
extern int ipf_perror_fd __P(( int, ioctlfunc_t, char *));
extern void ipf_rwlock_clean __P((void));
extern char *ipf_strerror __P((int));
extern void ipferror __P((int, char *));
extern int ipmon_parsefile __P((char *));
extern int ipmon_parsesome __P((FILE *));
extern int ipnat_addrule __P((int, ioctlfunc_t, void *));
extern int ipnat_parsefile __P((int, addfunc_t, ioctlfunc_t, char *));
extern int ipnat_parsesome __P((int, addfunc_t, ioctlfunc_t, FILE *));
extern int ippool_parsefile __P((int, char *, ioctlfunc_t));
extern int ippool_parsesome __P((int, FILE *, ioctlfunc_t));
extern int kmemcpywrap __P((void *, void *, size_t));
extern char *kvatoname __P((ipfunc_t, ioctlfunc_t));
extern int load_dstlist __P((struct ippool_dst *, ioctlfunc_t,
			     ipf_dstnode_t *));
extern int load_dstlistnode __P((int, char *, struct ipf_dstnode *,
				 ioctlfunc_t));
extern alist_t *load_file __P((char *));
extern int load_hash __P((struct iphtable_s *, struct iphtent_s *,
			  ioctlfunc_t));
extern int load_hashnode __P((int, char *, struct iphtent_s *, int,
			      ioctlfunc_t));
extern alist_t *load_http __P((char *));
extern int load_pool __P((struct ip_pool_s *list, ioctlfunc_t));
extern int load_poolnode __P((int, char *, ip_pool_node_t *, int, ioctlfunc_t));
extern alist_t *load_url __P((char *));
extern alist_t *make_range __P((int, struct in_addr, struct in_addr));
extern void mb_hexdump __P((mb_t *, FILE *));
extern ipfunc_t nametokva __P((char *, ioctlfunc_t));
extern void nat_setgroupmap __P((struct ipnat *));
extern int ntomask __P((int, int, u_32_t *));
extern u_32_t optname __P((char ***, u_short *, int));
extern wordtab_t *parsefields __P((wordtab_t *, char *));
extern int *parseipfexpr __P((char *, char **));
extern int parsewhoisline __P((char *, addrfamily_t *, addrfamily_t *));
extern void pool_close __P((void));
extern int pool_fd __P((void));
extern int pool_ioctl __P((ioctlfunc_t, ioctlcmd_t, void *));
extern int pool_open __P((void));
extern char *portname __P((int, int));
extern int pri_findname __P((char *));
extern char *pri_toname __P((int));
extern void print_toif __P((int, char *, char *, struct frdest *));
extern void printaps __P((ap_session_t *, int, int));
extern void printaddr __P((int, int, char *, int, u_32_t *, u_32_t *));
extern void printbuf __P((char *, int, int));
extern void printfieldhdr __P((wordtab_t *, wordtab_t *));
extern void printfr __P((struct frentry *, ioctlfunc_t));
extern struct iphtable_s *printhash __P((struct iphtable_s *, copyfunc_t,
					 char *, int, wordtab_t *));
extern struct iphtable_s *printhash_live __P((iphtable_t *, int, char *,
					      int, wordtab_t *));
extern ippool_dst_t *printdstl_live __P((ippool_dst_t *, int, char *,
					 int, wordtab_t *));
extern void printhashdata __P((iphtable_t *, int));
extern struct iphtent_s *printhashnode __P((struct iphtable_s *,
					    struct iphtent_s *,
					    copyfunc_t, int, wordtab_t *));
extern void printhost __P((int, u_32_t *));
extern void printhostmask __P((int, u_32_t *, u_32_t *));
extern void printip __P((int, u_32_t *));
extern void printlog __P((struct frentry *));
extern void printlookup __P((char *, i6addr_t *addr, i6addr_t *mask));
extern void printmask __P((int, u_32_t *));
extern void printnataddr __P((int, char *, nat_addr_t *, int));
extern void printnatfield __P((nat_t *, int));
extern void printnatside __P((char *, nat_stat_side_t *));
extern void printpacket __P((int, mb_t *));
extern void printpacket6 __P((int, mb_t *));
extern struct ippool_dst *printdstlist __P((struct ippool_dst *, copyfunc_t,
					    char *, int, ipf_dstnode_t *,
					    wordtab_t *));
extern void printdstlistdata __P((ippool_dst_t *, int));
extern ipf_dstnode_t *printdstlistnode __P((ipf_dstnode_t *, copyfunc_t,
					    int, wordtab_t *));
extern void printdstlistpolicy __P((ippool_policy_t));
extern struct ip_pool_s *printpool __P((struct ip_pool_s *, copyfunc_t,
					char *, int, wordtab_t *));
extern struct ip_pool_s *printpool_live __P((struct ip_pool_s *, int,
					     char *, int, wordtab_t *));
extern void printpooldata __P((ip_pool_t *, int));
extern void printpoolfield __P((void *, int, int));
extern struct ip_pool_node *printpoolnode __P((struct ip_pool_node *,
					       int, wordtab_t *));
extern void printproto __P((struct protoent *, int, struct ipnat *));
extern void printportcmp __P((int, struct frpcmp *));
extern void printstatefield __P((ipstate_t *, int));
extern void printtqtable __P((ipftq_t *));
extern void printtunable __P((ipftune_t *));
extern void printunit __P((int));
extern void optprint __P((u_short *, u_long, u_long));
#ifdef	USE_INET6
extern void optprintv6 __P((u_short *, u_long, u_long));
#endif
extern int remove_hash __P((struct iphtable_s *, ioctlfunc_t));
extern int remove_hashnode __P((int, char *, struct iphtent_s *, ioctlfunc_t));
extern int remove_pool __P((ip_pool_t *, ioctlfunc_t));
extern int remove_poolnode __P((int, char *, ip_pool_node_t *, ioctlfunc_t));
extern u_char tcpflags __P((char *));
extern void printc __P((struct frentry *));
extern void printC __P((int));
extern void emit __P((int, int, void *, struct frentry *));
extern u_char secbit __P((int));
extern u_char seclevel __P((char *));
extern void printfraginfo __P((char *, struct ipfr *));
extern void printifname __P((char *, char *, void *));
extern char *hostname __P((int, void *));
extern struct ipstate *printstate __P((struct ipstate *, int, u_long));
extern void printsbuf __P((char *));
extern void printnat __P((struct ipnat *, int));
extern void printactiveaddress __P((int, char *, i6addr_t *, char *));
extern void printactivenat __P((struct nat *, int, u_long));
extern void printhostmap __P((struct hostmap *, u_int));
extern void printtcpflags __P((u_32_t, u_32_t));
extern void printipfexpr __P((int *));
extern void printstatefield __P((ipstate_t *, int));
extern void printstatefieldhdr __P((int));
extern int sendtrap_v1_0 __P((int, char *, char *, int, time_t));
extern int sendtrap_v2_0 __P((int, char *, char *, int));
extern int vtof __P((int));

extern void set_variable __P((char *, char *));
extern char *get_variable __P((char *, char **, int));
extern void resetlexer __P((void));

extern void debug __P((int, char *, ...));
extern void verbose __P((int, char *, ...));
extern void ipfkdebug __P((char *, ...));
extern void ipfkverbose __P((char *, ...));

#if SOLARIS
extern int gethostname __P((char *, int ));
extern void sync __P((void));
#endif

#endif /* __IPF_H__ */
