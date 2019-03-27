/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * $Id$
 */

#ifndef	__IPT_H__
#define	__IPT_H__

#ifndef	__P
# define P_DEF
# ifdef	__STDC__
#  define	__P(x) x
# else
#  define	__P(x) ()
# endif
#endif

#include <fcntl.h>


struct	ipread	{
	int	(*r_open) __P((char *));
	int	(*r_close) __P((void));
	int	(*r_readip) __P((mb_t *, char **, int *));
	int	r_flags;
};

#define	R_DO_CKSUM	0x01

#ifdef P_DEF
# undef	__P
# undef	P_DEF
#endif

#endif /* __IPT_H__ */
