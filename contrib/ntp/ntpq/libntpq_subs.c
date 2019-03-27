/*****************************************************************************
 *
 *  libntpq_subs.c
 *
 *  This is the second part of the wrapper library for ntpq, the NTP query utility. 
 *  This library reuses the sourcecode from ntpq and exports a number
 *  of useful functions in a library that can be linked against applications
 *  that need to query the status of a running ntpd. The whole 
 *  communcation is based on mode 6 packets.
 *
 *  This source file exports the (private) functions from ntpq-subs.c 
 *
 ****************************************************************************/


#include "ntpq-subs.c"
#include "libntpq.h"


int ntpq_dogetassoc(void)
{
	
	if (dogetassoc(NULL))
		return numassoc;
	else
		return 0;	
}

/* the following functions are required internally by a number of libntpq functions 
 * and since they are defined as static in ntpq-subs.c, they need to be exported here
 */
 
char ntpq_decodeaddrtype(sockaddr_u *sock)
{
	return decodeaddrtype(sock);
}

int
ntpq_doquerylist(
	struct ntpq_varlist *vlist,
	int op,
	associd_t associd,
	int auth,
	u_short *rstatus,
	size_t *dsize,
	const char **datap
	)
{
	return doquerylist((struct varlist *)vlist, op, associd, auth,
			   rstatus, dsize, datap);
}

