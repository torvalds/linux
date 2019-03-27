/*
 * Not all machines define FD_SET in sys/types.h
 */ 
#ifndef NTP_SELECT_H
#define NTP_SELECT_H	/* note: tested by include/l_stdlib.h */

/* Was: (defined(RS6000)||defined(SYS_PTX))&&!defined(_BSD) */
/* Could say: !defined(FD_SET) && defined(HAVE_SYS_SELECT_H) */
/* except FD_SET can legitimately be a typedef... */
#if defined(HAVE_SYS_SELECT_H) && !defined(_BSD)
# ifndef SYS_VXWORKS
#  include <sys/select.h>
# else
#  include <sockLib.h>
extern	int	select(int width, fd_set *pReadFds, fd_set *pWriteFds,
		       fd_set *pExceptFds, struct timeval *pTimeOut);
# endif
#endif

#if !defined(FD_SET)
# define NFDBITS	32
# define FD_SETSIZE	32
# define FD_SET(n, p)	((p)->fds_bits[(n)/NFDBITS] |= (1 << ((n) % NFDBITS)))
# define FD_CLR(n, p)	((p)->fds_bits[(n)/NFDBITS] &= ~(1 << ((n) % NFDBITS)))
# define FD_ISSET(n, p)	((p)->fds_bits[(n)/NFDBITS] & (1 << ((n) % NFDBITS)))
# define FD_ZERO(p)	memset((p), 0, sizeof(*(p)))
#endif

#if defined(VMS)
typedef struct {
	unsigned int fds_bits[1];
} fd_set;
#endif

#endif	/* NTP_SELECT_H */
