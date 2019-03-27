/*
 * Copyright (c) 2003 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 * Contributed by Jose Marcio Martins da Cruz - Ecole des Mines de Paris
 *   Jose-Marcio.Martins@ensmp.fr
 */

/* a part of this code is based on inetd.c for which this copyright applies: */
/*
 * Copyright (c) 1983, 1991, 1993, 1994
 *      The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by the University of
 *      California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sendmail.h>
SM_RCSID("@(#)$Id: ratectrl.c,v 8.14 2013-11-22 20:51:56 ca Exp $")

/*
**  stuff included - given some warnings (inet_ntoa)
**	- surely not everything is needed
*/

#if NETINET || NETINET6
# include <arpa/inet.h>
#endif	/* NETINET || NETINET6 */

#include <sm/time.h>

#ifndef HASH_ALG
# define HASH_ALG	2
#endif /* HASH_ALG */

#ifndef RATECTL_DEBUG
# define RATECTL_DEBUG  0
#endif /* RATECTL_DEBUG */

/* forward declarations */
static int client_rate __P((time_t, SOCKADDR *, bool));
static int total_rate __P((time_t, bool));

/*
**  CONNECTION_RATE_CHECK - updates connection history data
**      and computes connection rate for the given host
**
**    Parameters:
**      hostaddr -- ip address of smtp client
**      e -- envelope
**
**    Returns:
**      true (always)
**
**    Side Effects:
**      updates connection history
**
**    Warnings:
**      For each connection, this call shall be
**      done only once with the value true for the
**      update parameter.
**      Typically, this call is done with the value
**      true by the father, and once again with
**      the value false by the children.
**
*/

bool
connection_rate_check(hostaddr, e)
	SOCKADDR *hostaddr;
	ENVELOPE *e;
{
	time_t now;
	int totalrate, clientrate;
	static int clientconn = 0;

	now = time(NULL);
#if RATECTL_DEBUG
	sm_syslog(LOG_INFO, NOQID, "connection_rate_check entering...");
#endif /* RATECTL_DEBUG */

	/* update server connection rate */
	totalrate = total_rate(now, e == NULL);
#if RATECTL_DEBUG
	sm_syslog(LOG_INFO, NOQID, "global connection rate: %d", totalrate);
#endif /* RATECTL_DEBUG */

	/* update client connection rate */
	clientrate = client_rate(now, hostaddr, e == NULL);

	if (e == NULL)
		clientconn = count_open_connections(hostaddr);

	if (e != NULL)
	{
		char s[16];

		sm_snprintf(s, sizeof(s), "%d", clientrate);
		macdefine(&e->e_macro, A_TEMP, macid("{client_rate}"), s);
		sm_snprintf(s, sizeof(s), "%d", totalrate);
		macdefine(&e->e_macro, A_TEMP, macid("{total_rate}"), s);
		sm_snprintf(s, sizeof(s), "%d", clientconn);
		macdefine(&e->e_macro, A_TEMP, macid("{client_connections}"),
				s);
	}
	return true;
}

/*
**  Data declarations needed to evaluate connection rate
*/

static int CollTime = 60;

/* this should be a power of 2, otherwise CPMHMASK doesn't work well */
#ifndef CPMHSIZE
# define CPMHSIZE	1024
#endif /* CPMHSIZE */

#define CPMHMASK	(CPMHSIZE-1)

#ifndef MAX_CT_STEPS
# define MAX_CT_STEPS	10
#endif /* MAX_CT_STEPS */

/*
**  time granularity: 10s (that's one "tick")
**  will be initialised to ConnectionRateWindowSize/CHTSIZE
**  before being used the first time
*/

static int ChtGran = -1;

#define CHTSIZE		6

/* Number of connections for a certain "tick" */
typedef struct CTime
{
	unsigned long	ct_Ticks;
	int		ct_Count;
}
CTime_T;

typedef struct CHash
{
#if NETINET6 && NETINET
	union
	{
		struct in_addr	c4_Addr;
		struct in6_addr	c6_Addr;
	} cu_Addr;
# define ch_Addr4	cu_Addr.c4_Addr
# define ch_Addr6	cu_Addr.c6_Addr
#else /* NETINET6 && NETINET */
# if NETINET6
	struct in6_addr	ch_Addr;
#  define ch_Addr6	ch_Addr
# else /* NETINET6 */
	struct in_addr ch_Addr;
#  define ch_Addr4	ch_Addr
# endif /* NETINET6 */
#endif /* NETINET6 && NETINET */

	int		ch_Family;
	time_t		ch_LTime;
	unsigned long	ch_colls;

	/* 6 buckets for ticks: 60s */
	CTime_T		ch_Times[CHTSIZE];
}
CHash_T;

static CHash_T CHashAry[CPMHSIZE];
static bool CHashAryOK = false;

/*
**  CLIENT_RATE - Evaluate connection rate per smtp client
**
**	Parameters:
**		now - current time in secs
**		saddr - client address
**		update - update data / check only
**
**	Returns:
**		connection rate (connections / ConnectionRateWindowSize)
**
**	Side effects:
**		update static global data
**
*/

static int
client_rate(now, saddr, update)
	 time_t now;
	 SOCKADDR *saddr;
	 bool update;
{
	unsigned int hv;
	int i;
	int cnt;
	bool coll;
	CHash_T *chBest = NULL;
	unsigned int ticks;

	cnt = 0;
	hv = 0xABC3D20F;
	if (ChtGran < 0)
		ChtGran = ConnectionRateWindowSize / CHTSIZE;
	if (ChtGran <= 0)
		ChtGran = 10;

	ticks = now / ChtGran;

	if (!CHashAryOK)
	{
		memset(CHashAry, 0, sizeof(CHashAry));
		CHashAryOK = true;
	}

	{
		char *p;
		int addrlen;
#if HASH_ALG != 1
		int c, d;
#endif /* HASH_ALG != 1 */

		switch (saddr->sa.sa_family)
		{
#if NETINET
		  case AF_INET:
			p = (char *)&saddr->sin.sin_addr;
			addrlen = sizeof(struct in_addr);
			break;
#endif /* NETINET */
#if NETINET6
		  case AF_INET6:
			p = (char *)&saddr->sin6.sin6_addr;
			addrlen = sizeof(struct in6_addr);
			break;
#endif /* NETINET6 */
		  default:
			/* should not happen */
			return -1;
		}

		/* compute hash value */
		for (i = 0; i < addrlen; ++i, ++p)
#if HASH_ALG == 1
			hv = (hv << 5) ^ (hv >> 23) ^ *p;
		hv = (hv ^ (hv >> 16));
#elif HASH_ALG == 2
		{
			d = *p;
			c = d;
			c ^= c<<6;
			hv += (c<<11) ^ (c>>1);
			hv ^= (d<<14) + (d<<7) + (d<<4) + d;
		}
#elif HASH_ALG == 3
		{
			hv = (hv << 4) + *p;
			d = hv & 0xf0000000;
			if (d != 0)
			{
				hv ^= (d >> 24);
				hv ^= d;
			}
		}
#else /* HASH_ALG == 1 */
			hv = ((hv << 1) ^ (*p & 0377)) % cctx->cc_size;
#endif /* HASH_ALG == 1 */
	}

	coll = true;
	for (i = 0; i < MAX_CT_STEPS; ++i)
	{
		CHash_T *ch = &CHashAry[(hv + i) & CPMHMASK];

#if NETINET
		if (saddr->sa.sa_family == AF_INET &&
		    ch->ch_Family == AF_INET &&
		    (saddr->sin.sin_addr.s_addr == ch->ch_Addr4.s_addr ||
		     ch->ch_Addr4.s_addr == 0))
		{
			chBest = ch;
			coll = false;
			break;
		}
#endif /* NETINET */
#if NETINET6
		if (saddr->sa.sa_family == AF_INET6 &&
		    ch->ch_Family == AF_INET6 &&
		    (IN6_ARE_ADDR_EQUAL(&saddr->sin6.sin6_addr,
				       &ch->ch_Addr6) != 0 ||
		     IN6_IS_ADDR_UNSPECIFIED(&ch->ch_Addr6)))
		{
			chBest = ch;
			coll = false;
			break;
		}
#endif /* NETINET6 */
		if (chBest == NULL || ch->ch_LTime == 0 ||
		    ch->ch_LTime < chBest->ch_LTime)
			chBest = ch;
	}

	/* Let's update data... */
	if (update)
	{
		if (coll && (now - chBest->ch_LTime < CollTime))
		{
			/*
			**  increment the number of collisions last
			**  CollTime for this client
			*/

			chBest->ch_colls++;

			/*
			**  Maybe shall log if collision rate is too high...
			**  and take measures to resize tables
			**  if this is the case
			*/
		}

		/*
		**  If it's not a match, then replace the data.
		**  Note: this purges the history of a colliding entry,
		**  which may cause "overruns", i.e., if two entries are
		**  "cancelling" each other out, then they may exceed
		**  the limits that are set. This might be mitigated a bit
		**  by the above "best of 5" function however.
		**
		**  Alternative approach: just use the old data, which may
		**  cause false positives however.
		**  To activate this, change deactivate following memset call.
		*/

		if (coll)
		{
#if NETINET
			if (saddr->sa.sa_family == AF_INET)
			{
				chBest->ch_Family = AF_INET;
				chBest->ch_Addr4 = saddr->sin.sin_addr;
			}
#endif /* NETINET */
#if NETINET6
			if (saddr->sa.sa_family == AF_INET6)
			{
				chBest->ch_Family = AF_INET6;
				chBest->ch_Addr6 = saddr->sin6.sin6_addr;
			}
#endif /* NETINET6 */
#if 1
			memset(chBest->ch_Times, '\0',
			       sizeof(chBest->ch_Times));
#endif /* 1 */
		}

		chBest->ch_LTime = now;
		{
			CTime_T *ct = &chBest->ch_Times[ticks % CHTSIZE];

			if (ct->ct_Ticks != ticks)
			{
				ct->ct_Ticks = ticks;
				ct->ct_Count = 0;
			}
			++ct->ct_Count;
		}
	}

	/* Now let's count connections on the window */
	for (i = 0; i < CHTSIZE; ++i)
	{
		CTime_T *ct = &chBest->ch_Times[i];

		if (ct->ct_Ticks <= ticks && ct->ct_Ticks >= ticks - CHTSIZE)
			cnt += ct->ct_Count;
	}

#if RATECTL_DEBUG
	sm_syslog(LOG_WARNING, NOQID,
		"cln: cnt=(%d), CHTSIZE=(%d), ChtGran=(%d)",
		cnt, CHTSIZE, ChtGran);
#endif /* RATECTL_DEBUG */
	return cnt;
}

/*
**  TOTAL_RATE - Evaluate global connection rate
**
**	Parameters:
**		now - current time in secs
**		update - update data / check only
**
**	Returns:
**		connection rate (connections / ConnectionRateWindowSize)
*/

static CTime_T srv_Times[CHTSIZE];
static bool srv_Times_OK = false;

static int
total_rate(now, update)
	 time_t now;
	 bool update;
{
	int i;
	int cnt = 0;
	CTime_T *ct;
	unsigned int ticks;

	if (ChtGran < 0)
		ChtGran = ConnectionRateWindowSize / CHTSIZE;
	if (ChtGran == 0)
		ChtGran = 10;
	ticks = now / ChtGran;
	if (!srv_Times_OK)
	{
		memset(srv_Times, 0, sizeof(srv_Times));
		srv_Times_OK = true;
	}

	/* Let's update data */
	if (update)
	{
		ct = &srv_Times[ticks % CHTSIZE];

		if (ct->ct_Ticks != ticks)
		{
			ct->ct_Ticks = ticks;
			ct->ct_Count = 0;
		}
		++ct->ct_Count;
	}

	/* Let's count connections on the window */
	for (i = 0; i < CHTSIZE; ++i)
	{
		ct = &srv_Times[i];

		if (ct->ct_Ticks <= ticks && ct->ct_Ticks >= ticks - CHTSIZE)
			cnt += ct->ct_Count;
	}

#if RATECTL_DEBUG
	sm_syslog(LOG_WARNING, NOQID,
		"srv: cnt=(%d), CHTSIZE=(%d), ChtGran=(%d)",
		 cnt, CHTSIZE, ChtGran);
#endif /* RATECTL_DEBUG */

	return cnt;
}
