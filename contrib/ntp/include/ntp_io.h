#ifndef NTP_IO_H
#define NTP_IO_H

#include "ntp_workimpl.h"

/*
 * POSIX says use <fnct.h> to get O_* symbols and 
 * SEEK_SET symbol form <unistd.h>.
 */
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#include <stdio.h>
#ifdef HAVE_SYS_FILE_H
# include <sys/file.h>
#endif
#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#endif
#if !defined(SEEK_SET) && defined(L_SET)
# define SEEK_SET L_SET
#endif

#ifdef SYS_WINNT
# include <io.h>
# include "win32_io.h"
#endif

#include <isc/boolean.h>
#include <isc/netaddr.h>

#if defined(HAVE_NETINET_IN_H) && defined(HAVE_NETINET_IP_H)
# include <netinet/in.h>
# ifdef HAVE_NETINET_IN_SYSTM_H
#  include <netinet/in_systm.h>
# endif
# include <netinet/ip.h>
#endif

#include "libntp.h"	/* This needs Something above for GETDTABLESIZE */

#include "ntp_keyacc.h"

/*
 * Define FNDELAY and FASYNC using O_NONBLOCK and O_ASYNC if we need
 * to (and can).  This is here initially for QNX, but may help for
 * others as well...
 */
#ifndef FNDELAY
# ifdef O_NONBLOCK
#  define FNDELAY O_NONBLOCK
# endif
#endif

#ifndef FASYNC
# ifdef O_ASYNC
#  define FASYNC O_ASYNC
# endif
#endif


/*
 * NIC rule match types
 */
typedef enum {
	MATCH_ALL,
	MATCH_IPV4,
	MATCH_IPV6,
	MATCH_WILDCARD,
	MATCH_IFNAME,
	MATCH_IFADDR
} nic_rule_match;

/*
 * NIC rule actions
 */
typedef enum {
	ACTION_LISTEN,
	ACTION_IGNORE,
	ACTION_DROP
} nic_rule_action;


extern int	qos;
SOCKET		move_fd(SOCKET fd);
isc_boolean_t	get_broadcastclient_flag(void);
extern void	sau_from_netaddr(sockaddr_u *, const isc_netaddr_t *);
extern void	add_nic_rule(nic_rule_match match_type,
			     const char *if_name, int prefixlen,
			     nic_rule_action action);
#ifndef HAVE_IO_COMPLETION_PORT
extern	void	maintain_activefds(int fd, int closing);
#else
#define		maintain_activefds(f, c)	do {} while (0)
#endif


#endif	/* NTP_IO_H */
