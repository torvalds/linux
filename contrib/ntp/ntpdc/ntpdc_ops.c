/*
 * ntpdc_ops.c - subroutines which are called to perform operations by
 *		 ntpdc
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <stddef.h>

#include "ntpdc.h"
#include "ntp_net.h"
#include "ntp_control.h"
#include "ntp_refclock.h"
#include "ntp_stdlib.h"

#include <ctype.h>
#ifdef HAVE_SYS_TIMEX_H
# include <sys/timex.h>
#endif
#if !defined(__bsdi__) && !defined(apollo)
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#endif

#include <arpa/inet.h>

/*
 * utility functions
 */
static	int	checkitems	(size_t, FILE *);
static	int	checkitemsize	(size_t, size_t);
static	int	check1item	(size_t, FILE *);

/*
 * Declarations for command handlers in here
 */
static	void	peerlist	(struct parse *, FILE *);
static	void	peers		(struct parse *, FILE *);
static void	doconfig	(struct parse *pcmd, FILE *fp, int mode, int refc);
static	void	dmpeers		(struct parse *, FILE *);
static	void	dopeers		(struct parse *, FILE *, int);
static	void	printpeer	(struct info_peer *, FILE *);
static	void	showpeer	(struct parse *, FILE *);
static	void	peerstats	(struct parse *, FILE *);
static	void	loopinfo	(struct parse *, FILE *);
static	void	sysinfo		(struct parse *, FILE *);
static	void	sysstats	(struct parse *, FILE *);
static	void	iostats		(struct parse *, FILE *);
static	void	memstats	(struct parse *, FILE *);
static	void	timerstats	(struct parse *, FILE *);
static	void	addpeer		(struct parse *, FILE *);
static	void	addserver	(struct parse *, FILE *);
static	void	addrefclock	(struct parse *, FILE *);
static	void	broadcast	(struct parse *, FILE *);
static	void	doconfig	(struct parse *, FILE *, int, int);
static	void	unconfig	(struct parse *, FILE *);
static	void	set		(struct parse *, FILE *);
static	void	sys_clear	(struct parse *, FILE *);
static	void	doset		(struct parse *, FILE *, int);
static	void	reslist		(struct parse *, FILE *);
static	void	new_restrict	(struct parse *, FILE *);
static	void	unrestrict	(struct parse *, FILE *);
static	void	delrestrict	(struct parse *, FILE *);
static	void	do_restrict	(struct parse *, FILE *, int);
static	void	monlist		(struct parse *, FILE *);
static	void	reset		(struct parse *, FILE *);
static	void	preset		(struct parse *, FILE *);
static	void	readkeys	(struct parse *, FILE *);
static	void	trustkey	(struct parse *, FILE *);
static	void	untrustkey	(struct parse *, FILE *);
static	void	do_trustkey	(struct parse *, FILE *, int);
static	void	authinfo	(struct parse *, FILE *);
static	void	traps		(struct parse *, FILE *);
static	void	addtrap		(struct parse *, FILE *);
static	void	clrtrap		(struct parse *, FILE *);
static	void	do_addclr_trap	(struct parse *, FILE *, int);
static	void	requestkey	(struct parse *, FILE *);
static	void	controlkey	(struct parse *, FILE *);
static	void	do_changekey	(struct parse *, FILE *, int);
static	void	ctlstats	(struct parse *, FILE *);
static	void	clockstat	(struct parse *, FILE *);
static	void	fudge		(struct parse *, FILE *);
static	void	clkbug		(struct parse *, FILE *);
static	void	kerninfo	(struct parse *, FILE *);
static	void	get_if_stats	(struct parse *, FILE *);
static	void	do_if_reload	(struct parse *, FILE *);

/*
 * Commands we understand.  Ntpdc imports this.
 */
struct xcmd opcmds[] = {
	{ "listpeers",	peerlist,	{ OPT|IP_VERSION, NO, NO, NO },
	  { "-4|-6", "", "", "" },
	  "display list of peers the server knows about [IP Version]" },
	{ "peers",	peers,	{ OPT|IP_VERSION, NO, NO, NO },
	  { "-4|-6", "", "", "" },
	  "display peer summary information [IP Version]" },
	{ "dmpeers",	dmpeers,	{ OPT|IP_VERSION, NO, NO, NO },
	  { "-4|-6", "", "", "" },
	  "display peer summary info the way Dave Mills likes it (IP Version)" },
	{ "showpeer",	showpeer, 	{ NTP_ADD, OPT|NTP_ADD, OPT|NTP_ADD, OPT|NTP_ADD},
	  { "peer_address", "peer2_addr", "peer3_addr", "peer4_addr" },
	  "display detailed information for one or more peers" },
	{ "pstats",	peerstats,	{ NTP_ADD, OPT|NTP_ADD, OPT|NTP_ADD, OPT|NTP_ADD },
	  { "peer_address", "peer2_addr", "peer3_addr", "peer4_addr" },
	  "display statistical information for one or more peers" },
	{ "loopinfo",	loopinfo,	{ OPT|NTP_STR, NO, NO, NO },
	  { "oneline|multiline", "", "", "" },
	  "display loop filter information" },
	{ "sysinfo",	sysinfo,	{ NO, NO, NO, NO },
	  { "", "", "", "" },
	  "display local server information" },
	{ "sysstats",	sysstats,	{ NO, NO, NO, NO },
	  { "", "", "", "" },
	  "display local server statistics" },
	{ "memstats",	memstats,	{ NO, NO, NO, NO },
	  { "", "", "", "" },
	  "display peer memory usage statistics" },
	{ "iostats",	iostats,	{ NO, NO, NO, NO },
	  { "", "", "", "" },
	  "display I/O subsystem statistics" },
	{ "timerstats",	timerstats,	{ NO, NO, NO, NO },
	  { "", "", "", "" },
	  "display event timer subsystem statistics" },
	{ "addpeer",	addpeer,	{ NTP_ADD, OPT|NTP_STR, OPT|NTP_STR, OPT|NTP_STR },
	  { "addr", "keyid", "version", "minpoll#|prefer|burst|iburst|'minpoll N'|'maxpoll N'|'keyid N'|'version N' ..." },
	  "configure a new peer association" },
	{ "addserver",	addserver,	{ NTP_ADD, OPT|NTP_STR, OPT|NTP_STR, OPT|NTP_STR },
	  { "addr", "keyid", "version", "minpoll#|prefer|burst|iburst|'minpoll N'|'maxpoll N'|'keyid N'|'version N' ..." },
	  "configure a new server" },
	{ "addrefclock",addrefclock,	{ NTP_ADD, OPT|NTP_UINT, OPT|NTP_STR, OPT|NTP_STR },
	  { "addr", "mode", "minpoll|prefer", "minpoll|prefer" },
	  "configure a new server" },
	{ "broadcast",	broadcast,	{ NTP_ADD, OPT|NTP_STR, OPT|NTP_STR, OPT|NTP_STR },
	  { "addr", "keyid", "version", "minpoll" },
	  "configure broadcasting time service" },
	{ "unconfig",	unconfig,	{ NTP_ADD, OPT|NTP_ADD, OPT|NTP_ADD, OPT|NTP_ADD },
	  { "peer_address", "peer2_addr", "peer3_addr", "peer4_addr" },
	  "unconfigure existing peer assocations" },
	{ "enable",	set,		{ NTP_STR, OPT|NTP_STR, OPT|NTP_STR, OPT|NTP_STR },
	  { "auth|bclient|monitor|pll|kernel|stats", "...", "...", "..." },
	  "set a system flag (auth, bclient, monitor, pll, kernel, stats)" },
	{ "disable",	sys_clear,	{ NTP_STR, OPT|NTP_STR, OPT|NTP_STR, OPT|NTP_STR },
	  { "auth|bclient|monitor|pll|kernel|stats", "...", "...", "..." },
	  "clear a system flag (auth, bclient, monitor, pll, kernel, stats)" },
	{ "reslist",	reslist,	{OPT|IP_VERSION, NO, NO, NO },
	  { "-4|-6", "", "", "" },
	  "display the server's restrict list" },
	{ "restrict",	new_restrict,	{ NTP_ADD, NTP_ADD, NTP_STR, OPT|NTP_STR },
	  { "address", "mask",
	    "ntpport|ignore|noserve|notrust|noquery|nomodify|nopeer|version|kod",
	    "..." },
	  "create restrict entry/add flags to entry" },
	{ "unrestrict", unrestrict,	{ NTP_ADD, NTP_ADD, NTP_STR, OPT|NTP_STR },
	  { "address", "mask",
	    "ntpport|ignore|noserve|notrust|noquery|nomodify|nopeer|version|kod",
	    "..." },
	  "remove flags from a restrict entry" },
	{ "delrestrict", delrestrict,	{ NTP_ADD, NTP_ADD, OPT|NTP_STR, NO },
	  { "address", "mask", "ntpport", "" },
	  "delete a restrict entry" },
	{ "monlist",	monlist,	{ OPT|NTP_INT, NO, NO, NO },
	  { "version", "", "", "" },
	  "display data the server's monitor routines have collected" },
	{ "reset",	reset,		{ NTP_STR, OPT|NTP_STR, OPT|NTP_STR, OPT|NTP_STR },
	  { "io|sys|mem|timer|auth|ctl|allpeers", "...", "...", "..." },
	  "reset various subsystem statistics counters" },
	{ "preset",	preset,		{ NTP_ADD, OPT|NTP_ADD, OPT|NTP_ADD, OPT|NTP_ADD },
	  { "peer_address", "peer2_addr", "peer3_addr", "peer4_addr" },
	  "reset stat counters associated with particular peer(s)" },
	{ "readkeys",	readkeys,	{ NO, NO, NO, NO },
	  { "", "", "", "" },
	  "request a reread of the keys file and re-init of system keys" },
	{ "trustedkey",	trustkey,	{ NTP_UINT, OPT|NTP_UINT, OPT|NTP_UINT, OPT|NTP_UINT },
	  { "keyid", "keyid", "keyid", "keyid" },
	  "add one or more key ID's to the trusted list" },
	{ "untrustedkey", untrustkey,	{ NTP_UINT, OPT|NTP_UINT, OPT|NTP_UINT, OPT|NTP_UINT },
	  { "keyid", "keyid", "keyid", "keyid" },
	  "remove one or more key ID's from the trusted list" },
	{ "authinfo",	authinfo,	{ NO, NO, NO, NO },
	  { "", "", "", "" },
	  "display the state of the authentication code" },
	{ "traps",	traps,		{ NO, NO, NO, NO },
	  { "", "", "", "" },
	  "display the traps set in the server" },
	{ "addtrap",	addtrap,	{ NTP_ADD, OPT|NTP_UINT, OPT|NTP_ADD, NO },
	  { "address", "port", "interface", "" },
	  "configure a trap in the server" },
	{ "clrtrap",	clrtrap,	{ NTP_ADD, OPT|NTP_UINT, OPT|NTP_ADD, NO },
	  { "address", "port", "interface", "" },
	  "remove a trap (configured or otherwise) from the server" },
	{ "requestkey",	requestkey,	{ NTP_UINT, NO, NO, NO },
	  { "keyid", "", "", "" },
	  "change the keyid the server uses to authenticate requests" },
	{ "controlkey",	controlkey,	{ NTP_UINT, NO, NO, NO },
	  { "keyid", "", "", "" },
	  "change the keyid the server uses to authenticate control messages" },
	{ "ctlstats",	ctlstats,	{ NO, NO, NO, NO },
	  { "", "", "", "" },
	  "display packet count statistics from the control module" },
	{ "clockstat",	clockstat,	{ NTP_ADD, OPT|NTP_ADD, OPT|NTP_ADD, OPT|NTP_ADD },
	  { "address", "address", "address", "address" },
	  "display clock status information" },
	{ "fudge",	fudge,		{ NTP_ADD, NTP_STR, NTP_STR, NO },
	  { "address", "time1|time2|val1|val2|flags", "value", "" },
	  "set/change one of a clock's fudge factors" },
	{ "clkbug",	clkbug,		{ NTP_ADD, OPT|NTP_ADD, OPT|NTP_ADD, OPT|NTP_ADD },
	  { "address", "address", "address", "address" },
	  "display clock debugging information" },
	{ "kerninfo",	kerninfo,	{ NO, NO, NO, NO },
	  { "", "", "", "" },
	  "display the kernel pll/pps variables" },
	{ "ifstats",	get_if_stats,	{ NO, NO, NO, NO },
	  { "", "", "", "" },
	  "list interface statistics" },
	{ "ifreload",	do_if_reload,	{ NO, NO, NO, NO },
	  { "", "", "", "" },
	  "reload interface configuration" },
	{ 0,		0,		{ NO, NO, NO, NO },
	  { "", "", "", "" }, "" }
};

/*
 * For quick string comparisons
 */
#define	STREQ(a, b)	(*(a) == *(b) && strcmp((a), (b)) == 0)

/*
 * SET_SS_LEN_IF_PRESENT - used by SET_ADDR, SET_ADDRS macros
 */

#ifdef ISC_PLATFORM_HAVESALEN
#define SET_SS_LEN_IF_PRESENT(psau)				\
	do {							\
		(psau)->sa.sa_len = SOCKLEN(psau);		\
	} while (0)
#else
#define SET_SS_LEN_IF_PRESENT(psau)	do { } while (0)
#endif

/*
 * SET_ADDR - setup address for v4/v6 as needed
 */
#define SET_ADDR(address, v6flag, v4addr, v6addr)		\
do {								\
	ZERO(address);						\
	if (v6flag) {						\
		AF(&(address)) = AF_INET6;			\
		SOCK_ADDR6(&(address)) = (v6addr);		\
	} else {						\
		AF(&(address)) = AF_INET;			\
		NSRCADR(&(address)) = (v4addr);			\
	}							\
	SET_SS_LEN_IF_PRESENT(&(address));			\
} while (0)


/*
 * SET_ADDRS - setup source and destination addresses for 
 * v4/v6 as needed
 */
#define SET_ADDRS(a1, a2, info, a1prefix, a2prefix)		\
do {								\
	ZERO(a1);						\
	ZERO(a2);						\
	if ((info)->v6_flag) {					\
		AF(&(a1)) = AF_INET6;				\
		AF(&(a2)) = AF_INET6;				\
		SOCK_ADDR6(&(a1)) = (info)->a1prefix##6;	\
		SOCK_ADDR6(&(a2)) = (info)->a2prefix##6;	\
	} else {						\
		AF(&(a1)) = AF_INET;				\
		AF(&(a2)) = AF_INET;				\
		NSRCADR(&(a1)) = (info)->a1prefix;		\
		NSRCADR(&(a2)) = (info)->a2prefix;		\
	}							\
	SET_SS_LEN_IF_PRESENT(&(a1));				\
	SET_SS_LEN_IF_PRESENT(&(a2));				\
} while (0)


/*
 * checkitems - utility to print a message if no items were returned
 */
static int
checkitems(
	size_t items,
	FILE *fp
	)
{
	if (items == 0) {
		(void) fprintf(fp, "No data returned in response to query\n");
		return 0;
	}
	return 1;
}


/*
 * checkitemsize - utility to print a message if the item size is wrong
 */
static int
checkitemsize(
	size_t itemsize,
	size_t expected
	)
{
	if (itemsize != expected) {
		(void) fprintf(stderr,
			       "***Incorrect item size returned by remote host (%lu should be %lu)\n",
			       (u_long)itemsize, (u_long)expected);
		return 0;
	}
	return 1;
}


/*
 * check1item - check to make sure we have exactly one item
 */
static int
check1item(
	size_t items,
	FILE *fp
	)
{
	if (items == 0) {
		(void) fprintf(fp, "No data returned in response to query\n");
		return 0;
	}
	if (items > 1) {
		(void) fprintf(fp, "Expected one item in response, got %lu\n",
			       (u_long)items);
		return 0;
	}
	return 1;
}


/*
 * peerlist - get a short list of peers
 */
/*ARGSUSED*/
static void
peerlist(
	struct parse *pcmd,
	FILE *fp
	)
{
	struct info_peer_list *plist;
	sockaddr_u paddr;
	size_t items;
	size_t itemsize;
	int res;

again:
	res = doquery(impl_ver, REQ_PEER_LIST, 0, 0, 0, (char *)NULL, &items,
		      &itemsize, (void *)&plist, 0, 
		      sizeof(struct info_peer_list));
	
	if (res == INFO_ERR_IMPL && impl_ver == IMPL_XNTPD) {
		impl_ver = IMPL_XNTPD_OLD;
		goto again;
	}

	if (res != 0)
	    return;

	if (!checkitems(items, fp))
	    return;

	if (!checkitemsize(itemsize, sizeof(struct info_peer_list)) &&
	    !checkitemsize(itemsize, v4sizeof(struct info_peer_list)))
	    return;

	while (items > 0) {
		SET_ADDR(paddr, plist->v6_flag, plist->addr, plist->addr6);
		if ((pcmd->nargs == 0) ||
		    ((pcmd->argval->ival == 6) && (plist->v6_flag != 0)) ||
		    ((pcmd->argval->ival == 4) && (plist->v6_flag == 0)))
			(void) fprintf(fp, "%-9s %s\n",
				modetoa(plist->hmode),
				nntohost(&paddr));
		plist++;
		items--;
	}
}


/*
 * peers - show peer summary
 */
static void
peers(
	struct parse *pcmd,
	FILE *fp
	)
{
	dopeers(pcmd, fp, 0);
}

/*
 * dmpeers - show peer summary, Dave Mills style
 */
static void
dmpeers(
	struct parse *pcmd,
	FILE *fp
	)
{
	dopeers(pcmd, fp, 1);
}


/*
 * peers - show peer summary
 */
/*ARGSUSED*/
static void
dopeers(
	struct parse *pcmd,
	FILE *fp,
	int dmstyle
	)
{
	struct info_peer_summary *plist;
	sockaddr_u dstadr;
	sockaddr_u srcadr;
	size_t items;
	size_t itemsize;
	int ntp_poll;
	int res;
	int c;
	l_fp tempts;

again:
	res = doquery(impl_ver, REQ_PEER_LIST_SUM, 0, 0, 0, (char *)NULL,
		      &items, &itemsize, (void *)&plist, 0, 
		      sizeof(struct info_peer_summary));
	
	if (res == INFO_ERR_IMPL && impl_ver == IMPL_XNTPD) {
		impl_ver = IMPL_XNTPD_OLD;
		goto again;
	}

	if (res != 0)
	    return;

	if (!checkitems(items, fp))
	    return;

	if (!checkitemsize(itemsize, sizeof(struct info_peer_summary)) &&
	    !checkitemsize(itemsize, v4sizeof(struct info_peer_summary)))
		return;

	(void) fprintf(fp,
		       "     remote           local      st poll reach  delay   offset    disp\n");
	(void) fprintf(fp,
		       "=======================================================================\n");
	while (items > 0) {
		if (!dmstyle) {
			if (plist->flags & INFO_FLAG_SYSPEER)
			    c = '*';
			else if (plist->hmode == MODE_ACTIVE)
			    c = '+';
			else if (plist->hmode == MODE_PASSIVE)
			    c = '-';
			else if (plist->hmode == MODE_CLIENT)
			    c = '=';
			else if (plist->hmode == MODE_BROADCAST)
			    c = '^';
			else if (plist->hmode == MODE_BCLIENT)
			    c = '~';
			else
			    c = ' ';
		} else {
			if (plist->flags & INFO_FLAG_SYSPEER)
			    c = '*';
			else if (plist->flags & INFO_FLAG_SHORTLIST)
			    c = '+';
			else if (plist->flags & INFO_FLAG_SEL_CANDIDATE)
			    c = '.';
			else
			    c = ' ';
		}
		NTOHL_FP(&(plist->offset), &tempts);
		ntp_poll = 1<<max(min3(plist->ppoll, plist->hpoll, NTP_MAXPOLL),
				  NTP_MINPOLL);
		SET_ADDRS(dstadr, srcadr, plist, dstadr, srcadr);
		if ((pcmd->nargs == 0) ||
		    ((pcmd->argval->ival == 6) && (plist->v6_flag != 0)) ||
		    ((pcmd->argval->ival == 4) && (plist->v6_flag == 0)))
			(void) fprintf(fp,
			    "%c%-15.15s %-15.15s %2u %4d  %3o %7.7s %9.9s %7.7s\n",
			    c, nntohost(&srcadr), stoa(&dstadr),
			    plist->stratum, ntp_poll, plist->reach,
			    fptoa(NTOHS_FP(plist->delay), 5),
			    lfptoa(&tempts, 6),
			    ufptoa(NTOHS_FP(plist->dispersion), 5));
		plist++;
		items--;
	}
}

/* Convert a refid & stratum (in host order) to a string */
static char *
refid_string(
	u_int32 refid,
	int stratum
	)
{
	if (stratum <= 1) {
		static char junk[5];
		junk[4] = 0;
		memcpy(junk, &refid, 4);
		return junk;
	}

	return numtoa(refid);
}

static void
print_pflag(
	FILE *	fp,
	u_int32	flags
	)
{
	static const char none[] = "";
	static const char comma[] = ",";
	const char *dlim;

	if (0 == flags) {
		fprintf(fp, " none\n");
		return;
	}
	dlim = none;
	if (flags & INFO_FLAG_SYSPEER) {
		fprintf(fp, " system_peer");
		dlim = comma;
	}
	if (flags & INFO_FLAG_CONFIG) {
		fprintf(fp, "%s config", dlim);
		dlim = comma;
	}
	if (flags & INFO_FLAG_REFCLOCK) {
		fprintf(fp, "%s refclock", dlim);
		dlim = comma;
	}
	if (flags & INFO_FLAG_AUTHENABLE) {
		fprintf(fp, "%s auth", dlim);
		dlim = comma;
	}
	if (flags & INFO_FLAG_PREFER) {
		fprintf(fp, "%s prefer", dlim);
		dlim = comma;
	}
	if (flags & INFO_FLAG_IBURST) {
		fprintf(fp, "%s iburst", dlim);
		dlim = comma;
	}
	if (flags & INFO_FLAG_BURST) {
		fprintf(fp, "%s burst", dlim);
		dlim = comma;
	}
	if (flags & INFO_FLAG_SEL_CANDIDATE) {
		fprintf(fp, "%s candidate", dlim);
		dlim = comma;
	}
	if (flags & INFO_FLAG_SHORTLIST) {
		fprintf(fp, "%s shortlist", dlim);
		dlim = comma;
	}
	fprintf(fp, "\n");
}
/*
 * printpeer - print detail information for a peer
 */
static void
printpeer(
	register struct info_peer *pp,
	FILE *fp
	)
{
	register int i;
	l_fp tempts;
	sockaddr_u srcadr, dstadr;
	
	SET_ADDRS(dstadr, srcadr, pp, dstadr, srcadr);
	
	(void) fprintf(fp, "remote %s, local %s\n",
		       stoa(&srcadr), stoa(&dstadr));
	(void) fprintf(fp, "hmode %s, pmode %s, stratum %d, precision %d\n",
		       modetoa(pp->hmode), modetoa(pp->pmode),
		       pp->stratum, pp->precision);
	
	(void) fprintf(fp,
		       "leap %c%c, refid [%s], rootdistance %s, rootdispersion %s\n",
		       pp->leap & 0x2 ? '1' : '0',
		       pp->leap & 0x1 ? '1' : '0',
		       refid_string(pp->refid, pp->stratum), fptoa(NTOHS_FP(pp->rootdelay), 5),
		       ufptoa(NTOHS_FP(pp->rootdispersion), 5));
	
	(void) fprintf(fp,
		       "ppoll %d, hpoll %d, keyid %lu, version %d, association %u\n",
		       pp->ppoll, pp->hpoll, (u_long)pp->keyid, pp->version, ntohs(pp->associd));

	(void) fprintf(fp,
		       "reach %03o, unreach %d, flash 0x%04x, ",
		       pp->reach, pp->unreach, pp->flash2);

	(void) fprintf(fp, "boffset %s, ttl/mode %d\n",
		       fptoa(NTOHS_FP(pp->estbdelay), 5), pp->ttl);
	
	(void) fprintf(fp, "timer %lds, flags", (long)ntohl(pp->timer));
	print_pflag(fp, pp->flags); 

	NTOHL_FP(&pp->reftime, &tempts);
	(void) fprintf(fp, "reference time:      %s\n",
		       prettydate(&tempts));
	NTOHL_FP(&pp->org, &tempts);
	(void) fprintf(fp, "originate timestamp: %s\n",
		       prettydate(&tempts));
	NTOHL_FP(&pp->rec, &tempts);
	(void) fprintf(fp, "receive timestamp:   %s\n",
		       prettydate(&tempts));
	NTOHL_FP(&pp->xmt, &tempts);
	(void) fprintf(fp, "transmit timestamp:  %s\n",
		       prettydate(&tempts));
	
	(void) fprintf(fp, "filter delay: ");
	for (i = 0; i < NTP_SHIFT; i++) {
		(void) fprintf(fp, " %-8.8s",
			       fptoa(NTOHS_FP(pp->filtdelay[i]), 5));
		if (i == (NTP_SHIFT>>1)-1)
		    (void) fprintf(fp, "\n              ");
	}
	(void) fprintf(fp, "\n");

	(void) fprintf(fp, "filter offset:");
	for (i = 0; i < NTP_SHIFT; i++) {
		NTOHL_FP(&pp->filtoffset[i], &tempts);
		(void) fprintf(fp, " %-8.8s", lfptoa(&tempts, 6));
		if (i == (NTP_SHIFT>>1)-1)
		    (void) fprintf(fp, "\n              ");
	}
	(void) fprintf(fp, "\n");

	(void) fprintf(fp, "filter order: ");
	for (i = 0; i < NTP_SHIFT; i++) {
		(void) fprintf(fp, " %-8d", pp->order[i]);
		if (i == (NTP_SHIFT>>1)-1)
		    (void) fprintf(fp, "\n              ");
	}
	(void) fprintf(fp, "\n");
	

	NTOHL_FP(&pp->offset, &tempts);
	(void) fprintf(fp,
		       "offset %s, delay %s, error bound %s, filter error %s\n",
		       lfptoa(&tempts, 6), fptoa(NTOHS_FP(pp->delay), 5),
		       ufptoa(NTOHS_FP(pp->dispersion), 5),
		       ufptoa(NTOHS_FP(pp->selectdisp), 5));
}


/*
 * showpeer - show detailed information for a peer
 */
static void
showpeer(
	struct parse *pcmd,
	FILE *fp
	)
{
	struct info_peer *pp;
	/* 4 is the maximum number of peers which will fit in a packet */
	struct info_peer_list *pl, plist[min(MAXARGS, 4)];
	size_t qitemlim;
	size_t qitems;
	size_t items;
	size_t itemsize;
	int res;
	int sendsize;

again:
	if (impl_ver == IMPL_XNTPD)
		sendsize = sizeof(struct info_peer_list);
	else
		sendsize = v4sizeof(struct info_peer_list);

	qitemlim = min(pcmd->nargs, COUNTOF(plist));
	for (qitems = 0, pl = plist; qitems < qitemlim; qitems++) {
		if (IS_IPV4(&pcmd->argval[qitems].netnum)) {
			pl->addr = NSRCADR(&pcmd->argval[qitems].netnum);
			if (impl_ver == IMPL_XNTPD)
				pl->v6_flag = 0;
		} else {
			if (impl_ver == IMPL_XNTPD_OLD) {
				fprintf(stderr,
				    "***Server doesn't understand IPv6 addresses\n");
				return;
			}
			pl->addr6 = SOCK_ADDR6(&pcmd->argval[qitems].netnum);
			pl->v6_flag = 1;
		}
		pl->port = (u_short)s_port;
		pl->hmode = pl->flags = 0;
		pl = (void *)((char *)pl + sendsize);
	}

	res = doquery(impl_ver, REQ_PEER_INFO, 0, qitems,
		      sendsize, (char *)plist, &items,
		      &itemsize, (void *)&pp, 0, sizeof(struct info_peer));
	
	if (res == INFO_ERR_IMPL && impl_ver == IMPL_XNTPD) {
		impl_ver = IMPL_XNTPD_OLD;
		goto again;
	}

	if (res != 0)
		return;

	if (!checkitems(items, fp))
		return;

	if (!checkitemsize(itemsize, sizeof(struct info_peer)) &&
	    !checkitemsize(itemsize, v4sizeof(struct info_peer)))
		return;

	while (items-- > 0) {
		printpeer(pp, fp);
		if (items > 0)
			fprintf(fp, "\n");
		pp++;
	}
}


/*
 * peerstats - return statistics for a peer
 */
static void
peerstats(
	struct parse *pcmd,
	FILE *fp
	)
{
	struct info_peer_stats *pp;
	/* 4 is the maximum number of peers which will fit in a packet */
	struct info_peer_list *pl, plist[min(MAXARGS, 4)];
	sockaddr_u src, dst;
	size_t qitemlim;
	size_t qitems;
	size_t items;
	size_t itemsize;
	int res;
	size_t sendsize;

again:
	if (impl_ver == IMPL_XNTPD)
		sendsize = sizeof(struct info_peer_list);
	else
		sendsize = v4sizeof(struct info_peer_list);

	ZERO(plist);

	qitemlim = min(pcmd->nargs, COUNTOF(plist));
	for (qitems = 0, pl = plist; qitems < qitemlim; qitems++) {
		if (IS_IPV4(&pcmd->argval[qitems].netnum)) {
			pl->addr = NSRCADR(&pcmd->argval[qitems].netnum);
			if (impl_ver == IMPL_XNTPD)
				pl->v6_flag = 0;
		} else {
			if (impl_ver == IMPL_XNTPD_OLD) {
				fprintf(stderr,
				    "***Server doesn't understand IPv6 addresses\n");
				return;
			}
			pl->addr6 = SOCK_ADDR6(&pcmd->argval[qitems].netnum);
			pl->v6_flag = 1;
		}
		pl->port = (u_short)s_port;
		pl->hmode = plist[qitems].flags = 0;
		pl = (void *)((char *)pl + sendsize);
	}

	res = doquery(impl_ver, REQ_PEER_STATS, 0, qitems,
		      sendsize, (char *)plist, &items,
		      &itemsize, (void *)&pp, 0, 
		      sizeof(struct info_peer_stats));
	
	if (res == INFO_ERR_IMPL && impl_ver == IMPL_XNTPD) {
		impl_ver = IMPL_XNTPD_OLD;
		goto again;
	}

	if (res != 0)
		return;

	if (!checkitems(items, fp))
	    return;

	if (!checkitemsize(itemsize, sizeof(struct info_peer_stats)) &&
	    !checkitemsize(itemsize, v4sizeof(struct info_peer_stats)))
	    return;

	while (items-- > 0) {
		ZERO_SOCK(&dst);
		ZERO_SOCK(&src);
		if (pp->v6_flag != 0) {
			AF(&dst) = AF_INET6;
			AF(&src) = AF_INET6;
			SOCK_ADDR6(&dst) = pp->dstadr6;
			SOCK_ADDR6(&src) = pp->srcadr6;
		} else {
			AF(&dst) = AF_INET;
			AF(&src) = AF_INET;
			NSRCADR(&dst) = pp->dstadr;
			NSRCADR(&src) = pp->srcadr;
		}
#ifdef ISC_PLATFORM_HAVESALEN
		src.sa.sa_len = SOCKLEN(&src);
		dst.sa.sa_len = SOCKLEN(&dst);
#endif
		fprintf(fp, "remote host:          %s\n",
			nntohost(&src));
		fprintf(fp, "local interface:      %s\n",
			stoa(&dst));
		fprintf(fp, "time last received:   %lus\n",
			(u_long)ntohl(pp->timereceived));
		fprintf(fp, "time until next send: %lus\n",
			(u_long)ntohl(pp->timetosend));
		fprintf(fp, "reachability change:  %lus\n",
			(u_long)ntohl(pp->timereachable));
		fprintf(fp, "packets sent:         %lu\n",
			(u_long)ntohl(pp->sent));
		fprintf(fp, "packets received:     %lu\n",
			(u_long)ntohl(pp->processed));
		fprintf(fp, "bad authentication:   %lu\n",
			(u_long)ntohl(pp->badauth));
		fprintf(fp, "bogus origin:         %lu\n",
			(u_long)ntohl(pp->bogusorg));
		fprintf(fp, "duplicate:            %lu\n",
			(u_long)ntohl(pp->oldpkt));
		fprintf(fp, "bad dispersion:       %lu\n",
			(u_long)ntohl(pp->seldisp));
		fprintf(fp, "bad reference time:   %lu\n",
			(u_long)ntohl(pp->selbroken));
		fprintf(fp, "candidate order:      %u\n",
			pp->candidate);
		if (items > 0)
			fprintf(fp, "\n");
		fprintf(fp, "flags:	");
		print_pflag(fp, ntohs(pp->flags));
		pp++;
	}
}


/*
 * loopinfo - show loop filter information
 */
static void
loopinfo(
	struct parse *pcmd,
	FILE *fp
	)
{
	struct info_loop *il;
	size_t items;
	size_t itemsize;
	int oneline = 0;
	int res;
	l_fp tempts;

	if (pcmd->nargs > 0) {
		if (STREQ(pcmd->argval[0].string, "oneline"))
		    oneline = 1;
		else if (STREQ(pcmd->argval[0].string, "multiline"))
		    oneline = 0;
		else {
			(void) fprintf(stderr, "How many lines?\n");
			return;
		}
	}

again:
	res = doquery(impl_ver, REQ_LOOP_INFO, 0, 0, 0, (char *)NULL,
		      &items, &itemsize, (void *)&il, 0, 
		      sizeof(struct info_loop));
	
	if (res == INFO_ERR_IMPL && impl_ver == IMPL_XNTPD) {
		impl_ver = IMPL_XNTPD_OLD;
		goto again;
	}

	if (res != 0)
	    return;

	if (!check1item(items, fp))
	    return;

	if (!checkitemsize(itemsize, sizeof(struct info_loop)))
	    return;

	if (oneline) {
		l_fp temp2ts;

		NTOHL_FP(&il->last_offset, &tempts);
		NTOHL_FP(&il->drift_comp, &temp2ts);

		(void) fprintf(fp,
			       "offset %s, frequency %s, time_const %ld, watchdog %ld\n",
			       lfptoa(&tempts, 6),
			       lfptoa(&temp2ts, 3),
			       (long)(int32)ntohl((u_long)il->compliance),
			       (u_long)ntohl((u_long)il->watchdog_timer));
	} else {
		NTOHL_FP(&il->last_offset, &tempts);
		(void) fprintf(fp, "offset:               %s s\n",
			       lfptoa(&tempts, 6));
		NTOHL_FP(&il->drift_comp, &tempts);
		(void) fprintf(fp, "frequency:            %s ppm\n",
			       lfptoa(&tempts, 3));
		(void) fprintf(fp, "poll adjust:          %ld\n",
			       (long)(int32)ntohl(il->compliance));
		(void) fprintf(fp, "watchdog timer:       %ld s\n",
			       (u_long)ntohl(il->watchdog_timer));
	}
}


/*
 * sysinfo - show current system state
 */
/*ARGSUSED*/
static void
sysinfo(
	struct parse *pcmd,
	FILE *fp
	)
{
	struct info_sys *is;
	sockaddr_u peeraddr;
	size_t items;
	size_t itemsize;
	int res;
	l_fp tempts;

again:
	res = doquery(impl_ver, REQ_SYS_INFO, 0, 0, 0, (char *)NULL,
		      &items, &itemsize, (void *)&is, 0,
		      sizeof(struct info_sys));
	
	if (res == INFO_ERR_IMPL && impl_ver == IMPL_XNTPD) {
		impl_ver = IMPL_XNTPD_OLD;
		goto again;
	}

	if (res != 0)
	    return;

	if (!check1item(items, fp))
	    return;

	if (!checkitemsize(itemsize, sizeof(struct info_sys)) &&
	    !checkitemsize(itemsize, v4sizeof(struct info_sys)))
	    return;

	SET_ADDR(peeraddr, is->v6_flag, is->peer, is->peer6);

	(void) fprintf(fp, "system peer:          %s\n", nntohost(&peeraddr));
	(void) fprintf(fp, "system peer mode:     %s\n", modetoa(is->peer_mode));
	(void) fprintf(fp, "leap indicator:       %c%c\n",
		       is->leap & 0x2 ? '1' : '0',
		       is->leap & 0x1 ? '1' : '0');
	(void) fprintf(fp, "stratum:              %d\n", (int)is->stratum);
	(void) fprintf(fp, "precision:            %d\n", (int)is->precision);
	(void) fprintf(fp, "root distance:        %s s\n",
		       fptoa(NTOHS_FP(is->rootdelay), 5));
	(void) fprintf(fp, "root dispersion:      %s s\n",
		       ufptoa(NTOHS_FP(is->rootdispersion), 5));
	(void) fprintf(fp, "reference ID:         [%s]\n",
		       refid_string(is->refid, is->stratum));
	NTOHL_FP(&is->reftime, &tempts);
	(void) fprintf(fp, "reference time:       %s\n", prettydate(&tempts));

	(void) fprintf(fp, "system flags:         ");
	if ((is->flags & (INFO_FLAG_BCLIENT | INFO_FLAG_AUTHENABLE |
	    INFO_FLAG_NTP | INFO_FLAG_KERNEL| INFO_FLAG_CAL |
	    INFO_FLAG_PPS_SYNC | INFO_FLAG_MONITOR | INFO_FLAG_FILEGEN)) == 0) {
		(void) fprintf(fp, "none\n");
	} else {
		if (is->flags & INFO_FLAG_BCLIENT)
		    (void) fprintf(fp, "bclient ");
		if (is->flags & INFO_FLAG_AUTHENTICATE)
		    (void) fprintf(fp, "auth ");
		if (is->flags & INFO_FLAG_MONITOR)
		    (void) fprintf(fp, "monitor ");
		if (is->flags & INFO_FLAG_NTP)
		    (void) fprintf(fp, "ntp ");
		if (is->flags & INFO_FLAG_KERNEL)
		    (void) fprintf(fp, "kernel ");
		if (is->flags & INFO_FLAG_FILEGEN)
		    (void) fprintf(fp, "stats ");
		if (is->flags & INFO_FLAG_CAL)
		    (void) fprintf(fp, "calibrate ");
		if (is->flags & INFO_FLAG_PPS_SYNC)
		    (void) fprintf(fp, "pps ");
		(void) fprintf(fp, "\n");
	}
	(void) fprintf(fp, "jitter:               %s s\n",
		       fptoa(ntohl(is->frequency), 6));
	(void) fprintf(fp, "stability:            %s ppm\n",
		       ufptoa(ntohl(is->stability), 3));
	(void) fprintf(fp, "broadcastdelay:       %s s\n",
		       fptoa(NTOHS_FP(is->bdelay), 6));
	NTOHL_FP(&is->authdelay, &tempts);
	(void) fprintf(fp, "authdelay:            %s s\n", lfptoa(&tempts, 6));
}


/*
 * sysstats - print system statistics
 */
/*ARGSUSED*/
static void
sysstats(
	struct parse *pcmd,
	FILE *fp
	)
{
	struct info_sys_stats *ss;
	size_t items;
	size_t itemsize;
	int res;

again:
	res = doquery(impl_ver, REQ_SYS_STATS, 0, 0, 0, (char *)NULL,
		      &items, &itemsize, (void *)&ss, 0, 
		      sizeof(struct info_sys_stats));
	
	if (res == INFO_ERR_IMPL && impl_ver == IMPL_XNTPD) {
		impl_ver = IMPL_XNTPD_OLD;
		goto again;
	}

	if (res != 0)
	    return;

	if (!check1item(items, fp))
	    return;

	if (itemsize != sizeof(struct info_sys_stats) &&
	    itemsize != sizeof(struct old_info_sys_stats)) {
		/* issue warning according to new structure size */
		checkitemsize(itemsize, sizeof(struct info_sys_stats));
		return;
	}
	fprintf(fp, "time since restart:     %lu\n",
		(u_long)ntohl(ss->timeup));
	fprintf(fp, "time since reset:       %lu\n",
		(u_long)ntohl(ss->timereset));
	fprintf(fp, "packets received:       %lu\n",
		(u_long)ntohl(ss->received));
	fprintf(fp, "packets processed:      %lu\n",
		(u_long)ntohl(ss->processed));
	fprintf(fp, "current version:        %lu\n",
		(u_long)ntohl(ss->newversionpkt));
	fprintf(fp, "previous version:       %lu\n",
		(u_long)ntohl(ss->oldversionpkt));
	fprintf(fp, "declined:               %lu\n",
		(u_long)ntohl(ss->unknownversion));
	fprintf(fp, "access denied:          %lu\n",
		(u_long)ntohl(ss->denied));
	fprintf(fp, "bad length or format:   %lu\n",
		(u_long)ntohl(ss->badlength));
	fprintf(fp, "bad authentication:     %lu\n",
		(u_long)ntohl(ss->badauth));
	if (itemsize != sizeof(struct info_sys_stats))
	    return;
	
	fprintf(fp, "rate exceeded:          %lu\n",
	       (u_long)ntohl(ss->limitrejected));
}



/*
 * iostats - print I/O statistics
 */
/*ARGSUSED*/
static void
iostats(
	struct parse *pcmd,
	FILE *fp
	)
{
	struct info_io_stats *io;
	size_t items;
	size_t itemsize;
	int res;

again:
	res = doquery(impl_ver, REQ_IO_STATS, 0, 0, 0, NULL, &items,
		      &itemsize, (void *)&io, 0, sizeof(*io));
	
	if (res == INFO_ERR_IMPL && impl_ver == IMPL_XNTPD) {
		impl_ver = IMPL_XNTPD_OLD;
		goto again;
	}

	if (res != 0)
		return;

	if (!check1item(items, fp))
		return;

	if (!checkitemsize(itemsize, sizeof(*io)))
		return;

	fprintf(fp, "time since reset:     %lu\n",
		(u_long)ntohl(io->timereset));
	fprintf(fp, "receive buffers:      %u\n",
		(u_int)ntohs(io->totalrecvbufs));
	fprintf(fp, "free receive buffers: %u\n",
		(u_int)ntohs(io->freerecvbufs));
	fprintf(fp, "used receive buffers: %u\n",
		(u_int)ntohs(io->fullrecvbufs));
	fprintf(fp, "low water refills:    %u\n",
		(u_int)ntohs(io->lowwater));
	fprintf(fp, "dropped packets:      %lu\n",
		(u_long)ntohl(io->dropped));
	fprintf(fp, "ignored packets:      %lu\n",
		(u_long)ntohl(io->ignored));
	fprintf(fp, "received packets:     %lu\n",
		(u_long)ntohl(io->received));
	fprintf(fp, "packets sent:         %lu\n",
		(u_long)ntohl(io->sent));
	fprintf(fp, "packets not sent:     %lu\n",
		(u_long)ntohl(io->notsent));
	fprintf(fp, "interrupts handled:   %lu\n",
		(u_long)ntohl(io->interrupts));
	fprintf(fp, "received by int:      %lu\n",
		(u_long)ntohl(io->int_received));
}


/*
 * memstats - print peer memory statistics
 */
/*ARGSUSED*/
static void
memstats(
	struct parse *pcmd,
	FILE *fp
	)
{
	struct info_mem_stats *mem;
	int i;
	size_t items;
	size_t itemsize;
	int res;

again:
	res = doquery(impl_ver, REQ_MEM_STATS, 0, 0, 0, NULL, &items,
		      &itemsize, (void *)&mem, 0, sizeof(*mem));
	
	if (res == INFO_ERR_IMPL && impl_ver == IMPL_XNTPD) {
		impl_ver = IMPL_XNTPD_OLD;
		goto again;
	}

	if (res != 0)
		return;

	if (!check1item(items, fp))
		return;

	if (!checkitemsize(itemsize, sizeof(*mem)))
		return;

	fprintf(fp, "time since reset:     %lu\n",
		(u_long)ntohl(mem->timereset));
	fprintf(fp, "total peer memory:    %u\n",
		(u_int)ntohs(mem->totalpeermem));
	fprintf(fp, "free peer memory:     %u\n",
		(u_int)ntohs(mem->freepeermem));
	fprintf(fp, "calls to findpeer:    %lu\n",
		(u_long)ntohl(mem->findpeer_calls));
	fprintf(fp, "new peer allocations: %lu\n",
		(u_long)ntohl(mem->allocations));
	fprintf(fp, "peer demobilizations: %lu\n",
		(u_long)ntohl(mem->demobilizations));

	fprintf(fp, "hash table counts:   ");
	for (i = 0; i < NTP_HASH_SIZE; i++) {
		fprintf(fp, "%4d", (int)mem->hashcount[i]);
		if ((i % 8) == 7 && i != (NTP_HASH_SIZE-1))
			fprintf(fp, "\n                     ");
	}
	fprintf(fp, "\n");
}



/*
 * timerstats - print timer statistics
 */
/*ARGSUSED*/
static void
timerstats(
	struct parse *pcmd,
	FILE *fp
	)
{
	struct info_timer_stats *tim;
	size_t items;
	size_t itemsize;
	int res;

again:
	res = doquery(impl_ver, REQ_TIMER_STATS, 0, 0, 0, NULL, &items,
		      &itemsize, (void *)&tim, 0, sizeof(*tim));
	
	if (res == INFO_ERR_IMPL && impl_ver == IMPL_XNTPD) {
		impl_ver = IMPL_XNTPD_OLD;
		goto again;
	}

	if (res != 0)
		return;

	if (!check1item(items, fp))
		return;

	if (!checkitemsize(itemsize, sizeof(*tim)))
		return;

	fprintf(fp, "time since reset:  %lu\n",
		(u_long)ntohl(tim->timereset));
	fprintf(fp, "alarms handled:    %lu\n",
		(u_long)ntohl(tim->alarms));
	fprintf(fp, "alarm overruns:    %lu\n",
		(u_long)ntohl(tim->overflows));
	fprintf(fp, "calls to transmit: %lu\n",
		(u_long)ntohl(tim->xmtcalls));
}


/*
 * addpeer - configure an active mode association
 */
static void
addpeer(
	struct parse *pcmd,
	FILE *fp
	)
{
	doconfig(pcmd, fp, MODE_ACTIVE, 0);
}


/*
 * addserver - configure a client mode association
 */
static void
addserver(
	struct parse *pcmd,
	FILE *fp
	)
{
	doconfig(pcmd, fp, MODE_CLIENT, 0);
}

/*
 * addrefclock - configure a reference clock association
 */
static void
addrefclock(
	struct parse *pcmd,
	FILE *fp
	)
{
	doconfig(pcmd, fp, MODE_CLIENT, 1);
}

/*
 * broadcast - configure a broadcast mode association
 */
static void
broadcast(
	struct parse *pcmd,
	FILE *fp
	)
{
	doconfig(pcmd, fp, MODE_BROADCAST, 0);
}


/*
 * config - configure a new peer association
 */
static void
doconfig(
	struct parse *pcmd,
	FILE *fp,
	int mode,
	int refc
	)
{
	struct conf_peer cpeer;
	size_t items;
	size_t itemsize;
	const char *dummy;
	u_long keyid;
	u_int version;
	u_char minpoll;
	u_char maxpoll;
	u_int flags;
	u_char cmode;
	int res;
	int sendsize;
	int numtyp;
	long val;

again:
	keyid = 0;
	version = 3;
	flags = 0;
	res = FALSE;
	cmode = 0;
	minpoll = NTP_MINDPOLL;
	maxpoll = NTP_MAXDPOLL;
	numtyp = 1;
	if (refc)
		numtyp = 5;

	if (impl_ver == IMPL_XNTPD)
		sendsize = sizeof(struct conf_peer);
	else
		sendsize = v4sizeof(struct conf_peer);

	items = 1;
	while (pcmd->nargs > (size_t)items) {
		if (STREQ(pcmd->argval[items].string, "prefer"))
			flags |= CONF_FLAG_PREFER;
		else if (STREQ(pcmd->argval[items].string, "burst"))
			flags |= CONF_FLAG_BURST;
		else if (STREQ(pcmd->argval[items].string, "iburst"))
			flags |= CONF_FLAG_IBURST;
		else if (!refc && STREQ(pcmd->argval[items].string, "keyid"))
			numtyp = 1;
		else if (!refc && STREQ(pcmd->argval[items].string, "version"))
			numtyp = 2;
		else if (STREQ(pcmd->argval[items].string, "minpoll"))
			numtyp = 3;
		else if (STREQ(pcmd->argval[items].string, "maxpoll"))
			numtyp = 4;
		else {
			if (!atoint(pcmd->argval[items].string, &val))
				numtyp = 0;
			switch (numtyp) {
			case 1:
				keyid = val;
				numtyp = 2;
				break;

			case 2:
				version = (u_int)val;
				numtyp = 0;
				break;

			case 3:
				minpoll = (u_char)val;
				numtyp = 0;
				break;

			case 4:
				maxpoll = (u_char)val;
				numtyp = 0;
				break;

			case 5:
				cmode = (u_char)val;
				numtyp = 0;
				break;

			default:
				fprintf(fp, "*** '%s' not understood\n",
					pcmd->argval[items].string);
				res = TRUE;
				numtyp = 0;
			}
			if (val < 0) {
				fprintf(stderr,
					"*** Value '%s' should be unsigned\n",
					pcmd->argval[items].string);
				res = TRUE;
			}
		}
		items++;
	}
	if (keyid > 0)
		flags |= CONF_FLAG_AUTHENABLE;
	if (version > NTP_VERSION || version < NTP_OLDVERSION) {
		fprintf(fp, "***invalid version number: %u\n",
			version);
		res = TRUE;
	}
	if (minpoll < NTP_MINPOLL || minpoll > NTP_MAXPOLL || 
	    maxpoll < NTP_MINPOLL || maxpoll > NTP_MAXPOLL || 
	    minpoll > maxpoll) {
		fprintf(fp, "***min/max-poll must be within %d..%d\n",
			NTP_MINPOLL, NTP_MAXPOLL);
		res = TRUE;
	}					

	if (res)
		return;

	ZERO(cpeer);

	if (IS_IPV4(&pcmd->argval[0].netnum)) {
		cpeer.peeraddr = NSRCADR(&pcmd->argval[0].netnum);
		if (impl_ver == IMPL_XNTPD)
			cpeer.v6_flag = 0;
	} else {
		if (impl_ver == IMPL_XNTPD_OLD) {
			fprintf(stderr,
			    "***Server doesn't understand IPv6 addresses\n");
			return;
		}
		cpeer.peeraddr6 = SOCK_ADDR6(&pcmd->argval[0].netnum);
		cpeer.v6_flag = 1;
	}
	cpeer.hmode = (u_char) mode;
	cpeer.keyid = keyid;
	cpeer.version = (u_char) version;
	cpeer.minpoll = minpoll;
	cpeer.maxpoll = maxpoll;
	cpeer.flags = (u_char)flags;
	cpeer.ttl = cmode;

	res = doquery(impl_ver, REQ_CONFIG, 1, 1,
		      sendsize, (char *)&cpeer, &items,
		      &itemsize, &dummy, 0, sizeof(struct conf_peer));
	
	if (res == INFO_ERR_IMPL && impl_ver == IMPL_XNTPD) {
		impl_ver = IMPL_XNTPD_OLD;
		goto again;
	}

	if (res == INFO_ERR_FMT) {
		(void) fprintf(fp,
		    "***Retrying command with old conf_peer size\n");
		res = doquery(impl_ver, REQ_CONFIG, 1, 1,
			      sizeof(struct old_conf_peer), (char *)&cpeer,
			      &items, &itemsize, &dummy, 0,
			      sizeof(struct conf_peer));
	}
	if (res == 0)
	    (void) fprintf(fp, "done!\n");
	return;
}


/*
 * unconfig - unconfigure some associations
 */
static void
unconfig(
	struct parse *pcmd,
	FILE *fp
	)
{
	/* 8 is the maximum number of peers which will fit in a packet */
	struct conf_unpeer *pl, plist[min(MAXARGS, 8)];
	size_t qitemlim;
	size_t qitems;
	size_t items;
	size_t itemsize;
	const char *dummy;
	int res;
	size_t sendsize;

again:
	if (impl_ver == IMPL_XNTPD)
		sendsize = sizeof(struct conf_unpeer);
	else
		sendsize = v4sizeof(struct conf_unpeer);

	qitemlim = min(pcmd->nargs, COUNTOF(plist));
	for (qitems = 0, pl = plist; qitems < qitemlim; qitems++) {
		if (IS_IPV4(&pcmd->argval[0].netnum)) {
			pl->peeraddr = NSRCADR(&pcmd->argval[qitems].netnum);
			if (impl_ver == IMPL_XNTPD)
				pl->v6_flag = 0;
		} else {
			if (impl_ver == IMPL_XNTPD_OLD) {
				fprintf(stderr,
				    "***Server doesn't understand IPv6 addresses\n");
				return;
			}
			pl->peeraddr6 =
			    SOCK_ADDR6(&pcmd->argval[qitems].netnum);
			pl->v6_flag = 1;
		}
		pl = (void *)((char *)pl + sendsize);
	}

	res = doquery(impl_ver, REQ_UNCONFIG, 1, qitems,
		      sendsize, (char *)plist, &items,
		      &itemsize, &dummy, 0, sizeof(struct conf_unpeer));
	
	if (res == INFO_ERR_IMPL && impl_ver == IMPL_XNTPD) {
		impl_ver = IMPL_XNTPD_OLD;
		goto again;
	}

	if (res == 0)
	    (void) fprintf(fp, "done!\n");
}


/*
 * set - set some system flags
 */
static void
set(
	struct parse *pcmd,
	FILE *fp
	)
{
	doset(pcmd, fp, REQ_SET_SYS_FLAG);
}


/*
 * clear - clear some system flags
 */
static void
sys_clear(
	struct parse *pcmd,
	FILE *fp
	)
{
	doset(pcmd, fp, REQ_CLR_SYS_FLAG);
}


/*
 * doset - set/clear system flags
 */
static void
doset(
	struct parse *pcmd,
	FILE *fp,
	int req
	)
{
	struct conf_sys_flags sys;
	size_t items;
	size_t itemsize;
	const char *dummy;
	int res;

	sys.flags = 0;
	res = 0;
	for (items = 0; (size_t)items < pcmd->nargs; items++) {
		if (STREQ(pcmd->argval[items].string, "auth"))
			sys.flags |= SYS_FLAG_AUTH;
		else if (STREQ(pcmd->argval[items].string, "bclient"))
			sys.flags |= SYS_FLAG_BCLIENT;
		else if (STREQ(pcmd->argval[items].string, "calibrate"))
			sys.flags |= SYS_FLAG_CAL;
		else if (STREQ(pcmd->argval[items].string, "kernel"))
			sys.flags |= SYS_FLAG_KERNEL;
		else if (STREQ(pcmd->argval[items].string, "monitor"))
			sys.flags |= SYS_FLAG_MONITOR;
		else if (STREQ(pcmd->argval[items].string, "ntp"))
			sys.flags |= SYS_FLAG_NTP;
		else if (STREQ(pcmd->argval[items].string, "pps"))
			sys.flags |= SYS_FLAG_PPS;
		else if (STREQ(pcmd->argval[items].string, "stats"))
			sys.flags |= SYS_FLAG_FILEGEN;
		else {
			(void) fprintf(fp, "Unknown flag %s\n",
			    pcmd->argval[items].string);
			res = 1;
		}
	}

	sys.flags = htonl(sys.flags);
	if (res || sys.flags == 0)
	    return;

again:
	res = doquery(impl_ver, req, 1, 1,
		      sizeof(struct conf_sys_flags), (char *)&sys, &items,
		      &itemsize, &dummy, 0, sizeof(struct conf_sys_flags));
	
	if (res == INFO_ERR_IMPL && impl_ver == IMPL_XNTPD) {
		impl_ver = IMPL_XNTPD_OLD;
		goto again;
	}

	if (res == 0)
	    (void) fprintf(fp, "done!\n");
}


/*
 * data for printing/interrpreting the restrict flags
 */
struct resflags {
  const char *str;
	int bit;
};

/* XXX: HMS: we apparently don't report set bits we do not recognize. */

static struct resflags resflagsV2[] = {
	{ "ignore",	0x001 },
	{ "noserve",	0x002 },
	{ "notrust",	0x004 },
	{ "noquery",	0x008 },
	{ "nomodify",	0x010 },
	{ "nopeer",	0x020 },
	{ "notrap",	0x040 },
	{ "lptrap",	0x080 },
	{ "limited",	0x100 },
	{ "",		0 }
};

static struct resflags resflagsV3[] = {
	{ "ignore",	RES_IGNORE },
	{ "noserve",	RES_DONTSERVE },
	{ "notrust",	RES_DONTTRUST },
	{ "noquery",	RES_NOQUERY },
	{ "nomodify",	RES_NOMODIFY },
	{ "nopeer",	RES_NOPEER },
	{ "notrap",	RES_NOTRAP },
	{ "lptrap",	RES_LPTRAP },
	{ "limited",	RES_LIMITED },
	{ "version",	RES_VERSION },
	{ "kod",	RES_KOD },
	{ "flake",	RES_FLAKE },

	{ "",		0 }
};

static struct resflags resmflags[] = {
	{ "ntpport",	RESM_NTPONLY },
	{ "interface",	RESM_INTERFACE },
	{ "source",	RESM_SOURCE },
	{ "",		0 }
};


/*
 * reslist - obtain and print the server's restrict list
 */
/*ARGSUSED*/
static void
reslist(
	struct parse *pcmd,
	FILE *fp
	)
{
	struct info_restrict *rl;
	sockaddr_u resaddr;
	sockaddr_u maskaddr;
	size_t items;
	size_t itemsize;
	int res;
	int skip;
	const char *addr;
	const char *mask;
	struct resflags *rf;
	u_int32 count;
	u_short rflags;
	u_short mflags;
	char flagstr[300];
	static const char *comma = ", ";

again:
	res = doquery(impl_ver, REQ_GET_RESTRICT, 0, 0, 0, (char *)NULL,
		      &items, &itemsize, (void *)&rl, 0, 
		      sizeof(struct info_restrict));
	
	if (res == INFO_ERR_IMPL && impl_ver == IMPL_XNTPD) {
		impl_ver = IMPL_XNTPD_OLD;
		goto again;
	}

	if (res != 0)
		return;

	if (!checkitems(items, fp))
		return;

	if (!checkitemsize(itemsize, sizeof(struct info_restrict)) &&
	    !checkitemsize(itemsize, v4sizeof(struct info_restrict)))
		return;

	fprintf(fp,
		"   address          mask            count        flags\n");
	fprintf(fp,
		"=====================================================================\n");

	while (items > 0) {
		SET_ADDRS(resaddr, maskaddr, rl, addr, mask);
		if (rl->v6_flag != 0) {
			addr = nntohost(&resaddr);
		} else {
			if (rl->mask == (u_int32)0xffffffff)
				addr = nntohost(&resaddr);
			else
				addr = stoa(&resaddr);
		}
		mask = stoa(&maskaddr);
		skip = 1;
		if ((pcmd->nargs == 0) ||
		    ((pcmd->argval->ival == 6) && (rl->v6_flag != 0)) ||
		    ((pcmd->argval->ival == 4) && (rl->v6_flag == 0)))
			skip = 0;
		count = ntohl(rl->count);
		rflags = ntohs(rl->rflags);
		mflags = ntohs(rl->mflags);
		flagstr[0] = '\0';

		res = 1;
		rf = &resmflags[0];
		while (rf->bit != 0) {
			if (mflags & rf->bit) {
				if (!res)
					strlcat(flagstr, comma,
						sizeof(flagstr));
				res = 0;
				strlcat(flagstr, rf->str,
					sizeof(flagstr));
			}
			rf++;
		}

		rf = (impl_ver == IMPL_XNTPD_OLD)
			 ? &resflagsV2[0]
			 : &resflagsV3[0];

		while (rf->bit != 0) {
			if (rflags & rf->bit) {
				if (!res)
					strlcat(flagstr, comma,
						sizeof(flagstr));
				res = 0;
				strlcat(flagstr, rf->str,
					sizeof(flagstr));
			}
			rf++;
		}

		if (flagstr[0] == '\0')
			strlcpy(flagstr, "none", sizeof(flagstr));

		if (!skip)
			fprintf(fp, "%-15.15s %-15.15s %9lu  %s\n",
				addr, mask, (u_long)count, flagstr);
		rl++;
		items--;
	}
}



/*
 * new_restrict - create/add a set of restrictions
 */
static void
new_restrict(
	struct parse *pcmd,
	FILE *fp
	)
{
	do_restrict(pcmd, fp, REQ_RESADDFLAGS);
}


/*
 * unrestrict - remove restriction flags from existing entry
 */
static void
unrestrict(
	struct parse *pcmd,
	FILE *fp
	)
{
	do_restrict(pcmd, fp, REQ_RESSUBFLAGS);
}


/*
 * delrestrict - delete an existing restriction
 */
static void
delrestrict(
	struct parse *pcmd,
	FILE *fp
	)
{
	do_restrict(pcmd, fp, REQ_UNRESTRICT);
}


/*
 * do_restrict - decode commandline restrictions and make the request
 */
static void
do_restrict(
	struct parse *pcmd,
	FILE *fp,
	int req_code
	)
{
	struct conf_restrict cres;
	size_t items;
	size_t itemsize;
	const char *dummy;
	u_int32 num;
	u_long bit;
	int i;
	size_t res;
	int err;
	int sendsize;

	/* Initialize cres */
	cres.addr = 0;
	cres.mask = 0;
	cres.flags = 0;
	cres.mflags = 0;
	cres.v6_flag = 0;

again:
	if (impl_ver == IMPL_XNTPD)
		sendsize = sizeof(struct conf_restrict);
	else
		sendsize = v4sizeof(struct conf_restrict);

	if (IS_IPV4(&pcmd->argval[0].netnum)) {
		cres.addr = NSRCADR(&pcmd->argval[0].netnum);
		cres.mask = NSRCADR(&pcmd->argval[1].netnum);
		if (impl_ver == IMPL_XNTPD)
			cres.v6_flag = 0;
	} else {
		if (impl_ver == IMPL_XNTPD_OLD) {
			fprintf(stderr,
				"***Server doesn't understand IPv6 addresses\n");
			return;
		}
		cres.addr6 = SOCK_ADDR6(&pcmd->argval[0].netnum);
		cres.v6_flag = 1;
	}
	cres.flags = 0;
	cres.mflags = 0;
	err = FALSE;
	for (res = 2; res < pcmd->nargs; res++) {
		if (STREQ(pcmd->argval[res].string, "ntpport")) {
			cres.mflags |= RESM_NTPONLY;
		} else {
			for (i = 0; resflagsV3[i].bit != 0; i++) {
				if (STREQ(pcmd->argval[res].string,
					  resflagsV3[i].str))
					break;
			}
			if (resflagsV3[i].bit != 0) {
				cres.flags |= resflagsV3[i].bit;
				if (req_code == REQ_UNRESTRICT) {
					fprintf(fp,
						"Flag %s inappropriate\n",
						resflagsV3[i].str);
					err = TRUE;
				}
			} else {
				fprintf(fp, "Unknown flag %s\n",
					pcmd->argval[res].string);
				err = TRUE;
			}
		}
	}
	cres.flags = htons(cres.flags);
	cres.mflags = htons(cres.mflags);

	/*
	 * Make sure mask for default address is zero.  Otherwise,
	 * make sure mask bits are contiguous.
	 */
	if (IS_IPV4(&pcmd->argval[0].netnum)) {
		if (cres.addr == 0) {
			cres.mask = 0;
		} else {
			num = ntohl(cres.mask);
			for (bit = 0x80000000; bit != 0; bit >>= 1)
				if ((num & bit) == 0)
					break;
			for ( ; bit != 0; bit >>= 1)
				if ((num & bit) != 0)
					break;
			if (bit != 0) {
				fprintf(fp, "Invalid mask %s\n",
					numtoa(cres.mask));
				err = TRUE;
			}
		}
	} else {
		/* XXX IPv6 sanity checking stuff */
	}

	if (err)
		return;

	res = doquery(impl_ver, req_code, 1, 1, sendsize, (char *)&cres,
		      &items, &itemsize, &dummy, 0, sizeof(cres));
	
	if (res == INFO_ERR_IMPL && impl_ver == IMPL_XNTPD) {
		impl_ver = IMPL_XNTPD_OLD;
		goto again;
	}

	if (res == 0)
	    (void) fprintf(fp, "done!\n");
	return;
}


/*
 * monlist - obtain and print the server's monitor data
 */
/*ARGSUSED*/
static void
monlist(
	struct parse *pcmd,
	FILE *fp
	)
{
	const char *struct_star;
	const struct info_monitor *ml;
	const struct info_monitor_1 *m1;
	const struct old_info_monitor *oml;
	sockaddr_u addr;
	sockaddr_u dstadr;
	size_t items;
	size_t itemsize;
	int res;
	int version = -1;

	if (pcmd->nargs > 0)
		version = pcmd->argval[0].ival;

again:
	res = doquery(impl_ver,
		      (version == 1 || version == -1) ? REQ_MON_GETLIST_1 :
		      REQ_MON_GETLIST, 0, 0, 0, NULL,
		      &items, &itemsize, &struct_star,
		      (version < 0) ? (1 << INFO_ERR_REQ) : 0, 
		      sizeof(struct info_monitor_1));

	if (res == INFO_ERR_IMPL && impl_ver == IMPL_XNTPD) {
		impl_ver = IMPL_XNTPD_OLD;
		goto again;
	}

	if (res == INFO_ERR_REQ && version < 0) 
		res = doquery(impl_ver, REQ_MON_GETLIST, 0, 0, 0, NULL,
			      &items, &itemsize, &struct_star, 0,
			      sizeof(struct info_monitor));
	
	if (res != 0)
		return;

	if (!checkitems(items, fp))
		return;

	if (itemsize == sizeof(struct info_monitor_1) ||
	    itemsize == v4sizeof(struct info_monitor_1)) {

	    m1 = (const void*)struct_star;
		fprintf(fp,
			"remote address          port local address      count m ver rstr avgint  lstint\n");
		fprintf(fp,
			"===============================================================================\n");
		while (items > 0) {
			SET_ADDRS(dstadr, addr, m1, daddr, addr);
			if ((pcmd->nargs == 0) ||
			    ((pcmd->argval->ival == 6) && (m1->v6_flag != 0)) ||
			    ((pcmd->argval->ival == 4) && (m1->v6_flag == 0)))
				fprintf(fp, 
				    "%-22.22s %5d %-15s %8lu %1u %1u %6lx %6lu %7lu\n",
				    nntohost(&addr), 
				    ntohs(m1->port),
				    stoa(&dstadr),
				    (u_long)ntohl(m1->count),
				    m1->mode,
				    m1->version,
				    (u_long)ntohl(m1->restr),
				    (u_long)ntohl(m1->avg_int),
				    (u_long)ntohl(m1->last_int));
			m1++;
			items--;
		}
	} else if (itemsize == sizeof(struct info_monitor) ||
	    itemsize == v4sizeof(struct info_monitor)) {

		ml = (const void *)struct_star;
		fprintf(fp,
			"     address               port     count mode ver rstr avgint  lstint\n");
		fprintf(fp,
			"===============================================================================\n");
		while (items > 0) {
			SET_ADDR(dstadr, ml->v6_flag, ml->addr, ml->addr6);
			if ((pcmd->nargs == 0) ||
			    ((pcmd->argval->ival == 6) && (ml->v6_flag != 0)) ||
			    ((pcmd->argval->ival == 4) && (ml->v6_flag == 0)))
				fprintf(fp,
				    "%-25.25s %5u %9lu %4u %2u %9lx %9lu %9lu\n",
				    nntohost(&dstadr),
				    ntohs(ml->port),
				    (u_long)ntohl(ml->count),
				    ml->mode,
				    ml->version,
				    (u_long)ntohl(ml->restr),
				    (u_long)ntohl(ml->avg_int),
				    (u_long)ntohl(ml->last_int));
			ml++;
			items--;
		}
	} else if (itemsize == sizeof(struct old_info_monitor)) {

		oml = (const void *)struct_star;
		fprintf(fp,
			"     address          port     count  mode version  lasttime firsttime\n");
		fprintf(fp,
			"======================================================================\n");
		while (items > 0) {
			SET_ADDR(dstadr, oml->v6_flag, oml->addr, oml->addr6);
			fprintf(fp, "%-20.20s %5u %9lu %4u   %3u %9lu %9lu\n",
				nntohost(&dstadr),
				ntohs(oml->port),
				(u_long)ntohl(oml->count),
				oml->mode,
				oml->version,
				(u_long)ntohl(oml->lasttime),
				(u_long)ntohl(oml->firsttime));
			oml++;
			items--;
		}
	} else {
		/* issue warning according to new info_monitor size */
		checkitemsize(itemsize, sizeof(struct info_monitor));
	}
}


/*
 * Mapping between command line strings and stat reset flags
 */
struct statreset {
	const char * const	str;
	const int		flag;
} sreset[] = {
	{ "allpeers",	RESET_FLAG_ALLPEERS },
	{ "io",		RESET_FLAG_IO },
	{ "sys",	RESET_FLAG_SYS },
	{ "mem",	RESET_FLAG_MEM },
	{ "timer",	RESET_FLAG_TIMER },
	{ "auth",	RESET_FLAG_AUTH },
	{ "ctl",	RESET_FLAG_CTL },
	{ "",		0 }
};

/*
 * reset - reset statistic counters
 */
static void
reset(
	struct parse *pcmd,
	FILE *fp
	)
{
	struct reset_flags rflags;
	size_t items;
	size_t itemsize;
	const char *dummy;
	int i;
	size_t res;
	int err;

	err = 0;
	rflags.flags = 0;
	for (res = 0; res < pcmd->nargs; res++) {
		for (i = 0; sreset[i].flag != 0; i++) {
			if (STREQ(pcmd->argval[res].string, sreset[i].str))
				break;
		}
		if (sreset[i].flag == 0) {
			fprintf(fp, "Flag %s unknown\n",
				pcmd->argval[res].string);
			err = 1;
		} else {
			rflags.flags |= sreset[i].flag;
		}
	}
	rflags.flags = htonl(rflags.flags);

	if (err) {
		(void) fprintf(fp, "Not done due to errors\n");
		return;
	}

again:
	res = doquery(impl_ver, REQ_RESET_STATS, 1, 1,
		      sizeof(struct reset_flags), (char *)&rflags, &items,
		      &itemsize, &dummy, 0, sizeof(struct reset_flags));
	
	if (res == INFO_ERR_IMPL && impl_ver == IMPL_XNTPD) {
		impl_ver = IMPL_XNTPD_OLD;
		goto again;
	}

	if (res == 0)
	    (void) fprintf(fp, "done!\n");
	return;
}



/*
 * preset - reset stat counters for particular peers
 */
static void
preset(
	struct parse *pcmd,
	FILE *fp
	)
{
	/* 8 is the maximum number of peers which will fit in a packet */
	struct conf_unpeer *pl, plist[min(MAXARGS, 8)];
	size_t qitemlim;
	size_t qitems;
	size_t items;
	size_t itemsize;
	const char *dummy;
	int res;
	size_t sendsize;

again:
	if (impl_ver == IMPL_XNTPD)
		sendsize = sizeof(struct conf_unpeer);
	else
		sendsize = v4sizeof(struct conf_unpeer);

	qitemlim = min(pcmd->nargs, COUNTOF(plist));
	for (qitems = 0, pl = plist; qitems < qitemlim; qitems++) {
		if (IS_IPV4(&pcmd->argval[qitems].netnum)) {
			pl->peeraddr = NSRCADR(&pcmd->argval[qitems].netnum);
			if (impl_ver == IMPL_XNTPD)
				pl->v6_flag = 0;
		} else {
			if (impl_ver == IMPL_XNTPD_OLD) {
				fprintf(stderr,
				    "***Server doesn't understand IPv6 addresses\n");
				return;
			}
			pl->peeraddr6 =
			    SOCK_ADDR6(&pcmd->argval[qitems].netnum);
			pl->v6_flag = 1;
		}
		pl = (void *)((char *)pl + sendsize);
	}

	res = doquery(impl_ver, REQ_RESET_PEER, 1, qitems,
		      sendsize, (char *)plist, &items,
		      &itemsize, &dummy, 0, sizeof(struct conf_unpeer));
	
	if (res == INFO_ERR_IMPL && impl_ver == IMPL_XNTPD) {
		impl_ver = IMPL_XNTPD_OLD;
		goto again;
	}

	if (res == 0)
	    (void) fprintf(fp, "done!\n");
}


/*
 * readkeys - request the server to reread the keys file
 */
/*ARGSUSED*/
static void
readkeys(
	struct parse *pcmd,
	FILE *fp
	)
{
	size_t items;
	size_t itemsize;
	const char *dummy;
	int res;

again:
	res = doquery(impl_ver, REQ_REREAD_KEYS, 1, 0, 0, (char *)0,
		      &items, &itemsize, &dummy, 0, sizeof(dummy));
	
	if (res == INFO_ERR_IMPL && impl_ver == IMPL_XNTPD) {
		impl_ver = IMPL_XNTPD_OLD;
		goto again;
	}

	if (res == 0)
	    (void) fprintf(fp, "done!\n");
	return;
}


/*
 * trustkey - add some keys to the trusted key list
 */
static void
trustkey(
	struct parse *pcmd,
	FILE *fp
	)
{
	do_trustkey(pcmd, fp, REQ_TRUSTKEY);
}


/*
 * untrustkey - remove some keys from the trusted key list
 */
static void
untrustkey(
	struct parse *pcmd,
	FILE *fp
	)
{
	do_trustkey(pcmd, fp, REQ_UNTRUSTKEY);
}


/*
 * do_trustkey - do grunge work of adding/deleting keys
 */
static void
do_trustkey(
	struct parse *pcmd,
	FILE *fp,
	int req
	)
{
	u_long keyids[MAXARGS];
	size_t i;
	size_t items;
	size_t itemsize;
	const char *dummy;
	int ritems;
	int res;

	ritems = 0;
	for (i = 0; i < pcmd->nargs; i++) {
		keyids[ritems++] = pcmd->argval[i].uval;
	}

again:
	res = doquery(impl_ver, req, 1, ritems, sizeof(u_long),
		      (char *)keyids, &items, &itemsize, &dummy, 0, 
		      sizeof(dummy));
	
	if (res == INFO_ERR_IMPL && impl_ver == IMPL_XNTPD) {
		impl_ver = IMPL_XNTPD_OLD;
		goto again;
	}

	if (res == 0)
	    (void) fprintf(fp, "done!\n");
	return;
}



/*
 * authinfo - obtain and print info about authentication
 */
/*ARGSUSED*/
static void
authinfo(
	struct parse *pcmd,
	FILE *fp
	)
{
	struct info_auth *ia;
	size_t items;
	size_t itemsize;
	int res;

again:
	res = doquery(impl_ver, REQ_AUTHINFO, 0, 0, 0, NULL, &items,
		      &itemsize, (void *)&ia, 0, sizeof(*ia));
	
	if (res == INFO_ERR_IMPL && impl_ver == IMPL_XNTPD) {
		impl_ver = IMPL_XNTPD_OLD;
		goto again;
	}

	if (res != 0)
		return;

	if (!check1item(items, fp))
		return;

	if (!checkitemsize(itemsize, sizeof(*ia)))
		return;

	fprintf(fp, "time since reset:     %lu\n",
		(u_long)ntohl(ia->timereset));
	fprintf(fp, "stored keys:          %lu\n",
		(u_long)ntohl(ia->numkeys));
	fprintf(fp, "free keys:            %lu\n",
		(u_long)ntohl(ia->numfreekeys));
	fprintf(fp, "key lookups:          %lu\n",
		(u_long)ntohl(ia->keylookups));
	fprintf(fp, "keys not found:       %lu\n",
		(u_long)ntohl(ia->keynotfound));
	fprintf(fp, "uncached keys:        %lu\n",
		(u_long)ntohl(ia->keyuncached));
	fprintf(fp, "encryptions:          %lu\n",
		(u_long)ntohl(ia->encryptions));
	fprintf(fp, "decryptions:          %lu\n",
		(u_long)ntohl(ia->decryptions));
	fprintf(fp, "expired keys:         %lu\n",
		(u_long)ntohl(ia->expired));
}



/*
 * traps - obtain and print a list of traps
 */
/*ARGSUSED*/
static void
traps(
	struct parse *pcmd,
	FILE *fp
	)
{
	size_t i;
	struct info_trap *it;
	sockaddr_u trap_addr, local_addr;
	size_t items;
	size_t itemsize;
	int res;

again:
	res = doquery(impl_ver, REQ_TRAPS, 0, 0, 0, NULL, &items,
		      &itemsize, (void *)&it, 0, sizeof(*it));
	
	if (res == INFO_ERR_IMPL && impl_ver == IMPL_XNTPD) {
		impl_ver = IMPL_XNTPD_OLD;
		goto again;
	}

	if (res != 0)
		return;

	if (!checkitems(items, fp))
		return;

	if (!checkitemsize(itemsize, sizeof(struct info_trap)) &&
	    !checkitemsize(itemsize, v4sizeof(struct info_trap)))
		return;

	for (i = 0; i < items; i++ ) {
		SET_ADDRS(trap_addr, local_addr, it, trap_address, local_address);
		fprintf(fp, "%saddress %s, port %d\n",
			(0 == i)
			    ? ""
			    : "\n",
			stoa(&trap_addr), ntohs(it->trap_port));
		fprintf(fp, "interface: %s, ",
			(0 == it->local_address)
			    ? "wildcard"
			    : stoa(&local_addr));
		if (ntohl(it->flags) & TRAP_CONFIGURED)
			fprintf(fp, "configured\n");
		else if (ntohl(it->flags) & TRAP_NONPRIO)
			fprintf(fp, "low priority\n");
		else
			fprintf(fp, "normal priority\n");
		
		fprintf(fp, "set for %ld secs, last set %ld secs ago\n",
			(long)ntohl(it->origtime),
			(long)ntohl(it->settime));
		fprintf(fp, "sequence %d, number of resets %ld\n",
			ntohs(it->sequence), (long)ntohl(it->resets));
	}
}


/*
 * addtrap - configure a trap
 */
static void
addtrap(
	struct parse *pcmd,
	FILE *fp
	)
{
	do_addclr_trap(pcmd, fp, REQ_ADD_TRAP);
}


/*
 * clrtrap - clear a trap from the server
 */
static void
clrtrap(
	struct parse *pcmd,
	FILE *fp
	)
{
	do_addclr_trap(pcmd, fp, REQ_CLR_TRAP);
}


/*
 * do_addclr_trap - do grunge work of adding/deleting traps
 */
static void
do_addclr_trap(
	struct parse *pcmd,
	FILE *fp,
	int req
	)
{
	struct conf_trap ctrap;
	size_t items;
	size_t itemsize;
	const char *dummy;
	int res;
	int sendsize;

again:
	if (impl_ver == IMPL_XNTPD)
		sendsize = sizeof(struct conf_trap);
	else
		sendsize = v4sizeof(struct conf_trap);

	if (IS_IPV4(&pcmd->argval[0].netnum)) {
		ctrap.trap_address = NSRCADR(&pcmd->argval[0].netnum);
		if (impl_ver == IMPL_XNTPD)
			ctrap.v6_flag = 0;
	} else {
		if (impl_ver == IMPL_XNTPD_OLD) {
			fprintf(stderr,
			    "***Server doesn't understand IPv6 addresses\n");
			return;
		}
		ctrap.trap_address6 = SOCK_ADDR6(&pcmd->argval[0].netnum);
		ctrap.v6_flag = 1;
	}
	ctrap.local_address = 0;
	ctrap.trap_port = htons(TRAPPORT);
	ctrap.unused = 0;

	if (pcmd->nargs > 1) {
		ctrap.trap_port	= htons((u_short)pcmd->argval[1].uval);
		if (pcmd->nargs > 2) {
			if (AF(&pcmd->argval[2].netnum) !=
			    AF(&pcmd->argval[0].netnum)) {
				fprintf(stderr,
				    "***Cannot mix IPv4 and IPv6 addresses\n");
				return;
			}
			if (IS_IPV4(&pcmd->argval[2].netnum))
				ctrap.local_address = NSRCADR(&pcmd->argval[2].netnum);
			else
				ctrap.local_address6 = SOCK_ADDR6(&pcmd->argval[2].netnum);
		}
	}

	res = doquery(impl_ver, req, 1, 1, sendsize,
		      (char *)&ctrap, &items, &itemsize, &dummy, 0, 
		      sizeof(struct conf_trap));
	
	if (res == INFO_ERR_IMPL && impl_ver == IMPL_XNTPD) {
		impl_ver = IMPL_XNTPD_OLD;
		goto again;
	}

	if (res == 0)
	    (void) fprintf(fp, "done!\n");
	return;
}



/*
 * requestkey - change the server's request key (a dangerous request)
 */
static void
requestkey(
	struct parse *pcmd,
	FILE *fp
	)
{
	do_changekey(pcmd, fp, REQ_REQUEST_KEY);
}


/*
 * controlkey - change the server's control key
 */
static void
controlkey(
	struct parse *pcmd,
	FILE *fp
	)
{
	do_changekey(pcmd, fp, REQ_CONTROL_KEY);
}



/*
 * do_changekey - do grunge work of changing keys
 */
static void
do_changekey(
	struct parse *pcmd,
	FILE *fp,
	int req
	)
{
	u_long key;
	size_t items;
	size_t itemsize;
	const char *dummy;
	int res;


	key = htonl((u_int32)pcmd->argval[0].uval);

again:
	res = doquery(impl_ver, req, 1, 1, sizeof(u_int32),
		      (char *)&key, &items, &itemsize, &dummy, 0, 
		      sizeof(dummy));
	
	if (res == INFO_ERR_IMPL && impl_ver == IMPL_XNTPD) {
		impl_ver = IMPL_XNTPD_OLD;
		goto again;
	}

	if (res == 0)
	    (void) fprintf(fp, "done!\n");
	return;
}



/*
 * ctlstats - obtain and print info about authentication
 */
/*ARGSUSED*/
static void
ctlstats(
	struct parse *pcmd,
	FILE *fp
	)
{
	struct info_control *ic;
	size_t items;
	size_t itemsize;
	int res;

again:
	res = doquery(impl_ver, REQ_GET_CTLSTATS, 0, 0, 0, NULL, &items,
		      &itemsize, (void *)&ic, 0, sizeof(*ic));
	
	if (res == INFO_ERR_IMPL && impl_ver == IMPL_XNTPD) {
		impl_ver = IMPL_XNTPD_OLD;
		goto again;
	}

	if (res != 0)
		return;

	if (!check1item(items, fp))
		return;

	if (!checkitemsize(itemsize, sizeof(*ic)))
		return;

	fprintf(fp, "time since reset:       %lu\n",
		(u_long)ntohl(ic->ctltimereset));
	fprintf(fp, "requests received:      %lu\n",
		(u_long)ntohl(ic->numctlreq));
	fprintf(fp, "responses sent:         %lu\n",
		(u_long)ntohl(ic->numctlresponses));
	fprintf(fp, "fragments sent:         %lu\n",
		(u_long)ntohl(ic->numctlfrags));
	fprintf(fp, "async messages sent:    %lu\n",
		(u_long)ntohl(ic->numasyncmsgs));
	fprintf(fp, "error msgs sent:        %lu\n",
		(u_long)ntohl(ic->numctlerrors));
	fprintf(fp, "total bad pkts:         %lu\n",
		(u_long)ntohl(ic->numctlbadpkts));
	fprintf(fp, "packet too short:       %lu\n",
		(u_long)ntohl(ic->numctltooshort));
	fprintf(fp, "response on input:      %lu\n",
		(u_long)ntohl(ic->numctlinputresp));
	fprintf(fp, "fragment on input:      %lu\n",
		(u_long)ntohl(ic->numctlinputfrag));
	fprintf(fp, "error set on input:     %lu\n",
		(u_long)ntohl(ic->numctlinputerr));
	fprintf(fp, "bad offset on input:    %lu\n",
		(u_long)ntohl(ic->numctlbadoffset));
	fprintf(fp, "bad version packets:    %lu\n",
		(u_long)ntohl(ic->numctlbadversion));
	fprintf(fp, "data in pkt too short:  %lu\n",
		(u_long)ntohl(ic->numctldatatooshort));
	fprintf(fp, "unknown op codes:       %lu\n",
		(u_long)ntohl(ic->numctlbadop));
}


/*
 * clockstat - get and print clock status information
 */
static void
clockstat(
	struct parse *pcmd,
	FILE *fp
	)
{
	struct info_clock *cl;
	/* 8 is the maximum number of clocks which will fit in a packet */
	u_long clist[min(MAXARGS, 8)];
	size_t qitemlim;
	size_t qitems;
	size_t items;
	size_t itemsize;
	int res;
	l_fp ts;
	struct clktype *clk;

	qitemlim = min(pcmd->nargs, COUNTOF(clist));
	for (qitems = 0; qitems < qitemlim; qitems++)
		clist[qitems] = NSRCADR(&pcmd->argval[qitems].netnum);

again:
	res = doquery(impl_ver, REQ_GET_CLOCKINFO, 0, qitems,
		      sizeof(u_int32), (char *)clist, &items,
		      &itemsize, (void *)&cl, 0, sizeof(struct info_clock));
	
	if (res == INFO_ERR_IMPL && impl_ver == IMPL_XNTPD) {
		impl_ver = IMPL_XNTPD_OLD;
		goto again;
	}

	if (res != 0)
		return;

	if (!checkitems(items, fp))
		return;

	if (!checkitemsize(itemsize, sizeof(struct info_clock)))
		return;

	while (items-- > 0) {
		(void) fprintf(fp, "clock address:        %s\n",
			       numtoa(cl->clockadr));
		for (clk = clktypes; clk->code >= 0; clk++)
		    if (clk->code == cl->type)
			break;
		if (clk->code >= 0)
		    (void) fprintf(fp, "clock type:           %s\n",
				   clk->clocktype);
		else
		    (void) fprintf(fp, "clock type:           unknown type (%d)\n",
				   cl->type);
		(void) fprintf(fp, "last event:           %d\n",
			       cl->lastevent);
		(void) fprintf(fp, "current status:       %d\n",
			       cl->currentstatus);
		(void) fprintf(fp, "number of polls:      %lu\n",
			       (u_long)ntohl(cl->polls));
		(void) fprintf(fp, "no response to poll:  %lu\n",
			       (u_long)ntohl(cl->noresponse));
		(void) fprintf(fp, "bad format responses: %lu\n",
			       (u_long)ntohl(cl->badformat));
		(void) fprintf(fp, "bad data responses:   %lu\n",
			       (u_long)ntohl(cl->baddata));
		(void) fprintf(fp, "running time:         %lu\n",
			       (u_long)ntohl(cl->timestarted));
		NTOHL_FP(&cl->fudgetime1, &ts);
		(void) fprintf(fp, "fudge time 1:         %s\n",
			       lfptoa(&ts, 6));
		NTOHL_FP(&cl->fudgetime2, &ts);
		(void) fprintf(fp, "fudge time 2:         %s\n",
			       lfptoa(&ts, 6));
		(void) fprintf(fp, "stratum:              %ld\n",
			       (u_long)ntohl(cl->fudgeval1));
		/* [Bug3527] Backward Incompatible: cl->fudgeval2 is
		 * a string, instantiated via memcpy() so there is no
		 * endian issue to correct.
		 */
#ifdef DISABLE_BUG3527_FIX
		(void) fprintf(fp, "reference ID:         %s\n",
			       refid_string(ntohl(cl->fudgeval2), 0));
#else
		(void) fprintf(fp, "reference ID:         %s\n",
			       refid_string(cl->fudgeval2, 0));
#endif
		(void) fprintf(fp, "fudge flags:          0x%x\n",
			       cl->flags);

		if (items > 0)
		    (void) fprintf(fp, "\n");
		cl++;
	}
}


/*
 * fudge - set clock fudge factors
 */
static void
fudge(
	struct parse *pcmd,
	FILE *fp
	)
{
	struct conf_fudge fudgedata;
	size_t items;
	size_t itemsize;
	const char *dummy;
	l_fp ts;
	int res;
	long val;
	u_long u_val;
	int err;


	err = 0;
	ZERO(fudgedata);
	fudgedata.clockadr = NSRCADR(&pcmd->argval[0].netnum);

	if (STREQ(pcmd->argval[1].string, "time1")) {
		fudgedata.which = htonl(FUDGE_TIME1);
		if (!atolfp(pcmd->argval[2].string, &ts))
		    err = 1;
		else
		    NTOHL_FP(&ts, &fudgedata.fudgetime);
	} else if (STREQ(pcmd->argval[1].string, "time2")) {
		fudgedata.which = htonl(FUDGE_TIME2);
		if (!atolfp(pcmd->argval[2].string, &ts))
		    err = 1;
		else
		    NTOHL_FP(&ts, &fudgedata.fudgetime);
	} else if (STREQ(pcmd->argval[1].string, "val1")) {
		fudgedata.which = htonl(FUDGE_VAL1);
		if (!atoint(pcmd->argval[2].string, &val))
		    err = 1;
		else
		    fudgedata.fudgeval_flags = htonl(val);
	} else if (STREQ(pcmd->argval[1].string, "val2")) {
		fudgedata.which = htonl(FUDGE_VAL2);
		if (!atoint(pcmd->argval[2].string, &val))
		    err = 1;
		else
		    fudgedata.fudgeval_flags = htonl((u_int32)val);
	} else if (STREQ(pcmd->argval[1].string, "flags")) {
		fudgedata.which = htonl(FUDGE_FLAGS);
		if (!hextoint(pcmd->argval[2].string, &u_val))
		    err = 1;
		else
		    fudgedata.fudgeval_flags = htonl((u_int32)(u_val & 0xf));
	} else {
		(void) fprintf(stderr, "What fudge is %s?\n",
			       pcmd->argval[1].string);
		return;
	}

	if (err) {
		(void) fprintf(stderr, "Unknown fudge parameter %s\n",
			       pcmd->argval[2].string);
		return;
	}

again:
	res = doquery(impl_ver, REQ_SET_CLKFUDGE, 1, 1,
		      sizeof(struct conf_fudge), (char *)&fudgedata, &items,
		      &itemsize, &dummy, 0, sizeof(dummy));

	if (res == INFO_ERR_IMPL && impl_ver == IMPL_XNTPD) {
		impl_ver = IMPL_XNTPD_OLD;
		goto again;
	}

	if (res == 0)
	    (void) fprintf(fp, "done!\n");
	return;
}

/*
 * clkbug - get and print clock debugging information
 */
static void
clkbug(
	struct parse *pcmd,
	FILE *fp
	)
{
	register int i;
	register int n;
	register u_int32 s;
	struct info_clkbug *cl;
	/* 8 is the maximum number of clocks which will fit in a packet */
	u_long clist[min(MAXARGS, 8)];
	u_int32 ltemp;
	size_t qitemlim;
	size_t qitems;
	size_t items;
	size_t itemsize;
	int res;
	int needsp;
	l_fp ts;

	qitemlim = min(pcmd->nargs, COUNTOF(clist));
	for (qitems = 0; qitems < qitemlim; qitems++)
		clist[qitems] = NSRCADR(&pcmd->argval[qitems].netnum);

again:
	res = doquery(impl_ver, REQ_GET_CLKBUGINFO, 0, qitems,
		      sizeof(u_int32), (char *)clist, &items,
		      &itemsize, (void *)&cl, 0, sizeof(struct info_clkbug));
	
	if (res == INFO_ERR_IMPL && impl_ver == IMPL_XNTPD) {
		impl_ver = IMPL_XNTPD_OLD;
		goto again;
	}

	if (res != 0)
		return;

	if (!checkitems(items, fp))
		return;

	if (!checkitemsize(itemsize, sizeof(struct info_clkbug)))
		return;

	while (items-- > 0) {
		(void) fprintf(fp, "clock address:        %s\n",
			       numtoa(cl->clockadr));
		n = (int)cl->nvalues;
		(void) fprintf(fp, "values: %d", n);
		s = ntohs(cl->svalues);
		if (n > NUMCBUGVALUES)
		    n = NUMCBUGVALUES;
		for (i = 0; i < n; i++) {
			ltemp = ntohl(cl->values[i]);
			ltemp &= 0xffffffff;	/* HMS: This does nothing now */
			if ((i & 0x3) == 0)
			    (void) fprintf(fp, "\n");
			if (s & (1 << i))
			    (void) fprintf(fp, "%12ld", (u_long)ltemp);
			else
			    (void) fprintf(fp, "%12lu", (u_long)ltemp);
		}
		(void) fprintf(fp, "\n");

		n = (int)cl->ntimes;
		(void) fprintf(fp, "times: %d", n);
		s = ntohl(cl->stimes);
		if (n > NUMCBUGTIMES)
		    n = NUMCBUGTIMES;
		needsp = 0;
		for (i = 0; i < n; i++) {
			if ((i & 0x1) == 0) {
			    (void) fprintf(fp, "\n");
			} else {
				for (;needsp > 0; needsp--)
				    putc(' ', fp);
			}
			NTOHL_FP(&cl->times[i], &ts);
			if (s & (1 << i)) {
				(void) fprintf(fp, "%17s",
					       lfptoa(&ts, 6));
				needsp = 22;
			} else {
				(void) fprintf(fp, "%37s",
					       uglydate(&ts));
				needsp = 2;
			}
		}
		(void) fprintf(fp, "\n");
		if (items > 0) {
			cl++;
			(void) fprintf(fp, "\n");
		}
	}
}


/*
 * kerninfo - display the kernel pll/pps variables
 */
static void
kerninfo(
	struct parse *pcmd,
	FILE *fp
	)
{
	struct info_kernel *ik;
	size_t items;
	size_t itemsize;
	int res;
	unsigned status;
	double tscale_usec = 1e-6, tscale_unano = 1e-6;

again:
	res = doquery(impl_ver, REQ_GET_KERNEL, 0, 0, 0, (char *)NULL,
		      &items, &itemsize, (void *)&ik, 0, 
		      sizeof(struct info_kernel));

	if (res == INFO_ERR_IMPL && impl_ver == IMPL_XNTPD) {
		impl_ver = IMPL_XNTPD_OLD;
		goto again;
	}

	if (res != 0)
	    return;
	if (!check1item(items, fp))
	    return;
	if (!checkitemsize(itemsize, sizeof(struct info_kernel)))
	    return;

	status = ntohs(ik->status) & 0xffff;
	/*
	 * pll variables. We know more than we should about the NANO bit.
	 */
#ifdef STA_NANO
	if (status & STA_NANO)
		tscale_unano = 1e-9;
#endif
	(void)fprintf(fp, "pll offset:           %g s\n",
	    (int32)ntohl(ik->offset) * tscale_unano);
	(void)fprintf(fp, "pll frequency:        %s ppm\n",
	    fptoa((s_fp)ntohl(ik->freq), 3));
	(void)fprintf(fp, "maximum error:        %g s\n",
	    (u_long)ntohl(ik->maxerror) * tscale_usec);
	(void)fprintf(fp, "estimated error:      %g s\n",
	    (u_long)ntohl(ik->esterror) * tscale_usec);
	(void)fprintf(fp, "status:               %04x ", status);
#ifdef STA_PLL
	if (status & STA_PLL) (void)fprintf(fp, " pll");
#endif
#ifdef STA_PPSFREQ
	if (status & STA_PPSFREQ) (void)fprintf(fp, " ppsfreq");
#endif
#ifdef STA_PPSTIME
	if (status & STA_PPSTIME) (void)fprintf(fp, " ppstime");
#endif
#ifdef STA_FLL
	if (status & STA_FLL) (void)fprintf(fp, " fll");
#endif
#ifdef STA_INS
	if (status & STA_INS) (void)fprintf(fp, " ins");
#endif
#ifdef STA_DEL
	if (status & STA_DEL) (void)fprintf(fp, " del");
#endif
#ifdef STA_UNSYNC
	if (status & STA_UNSYNC) (void)fprintf(fp, " unsync");
#endif
#ifdef STA_FREQHOLD
	if (status & STA_FREQHOLD) (void)fprintf(fp, " freqhold");
#endif
#ifdef STA_PPSSIGNAL
	if (status & STA_PPSSIGNAL) (void)fprintf(fp, " ppssignal");
#endif
#ifdef STA_PPSJITTER
	if (status & STA_PPSJITTER) (void)fprintf(fp, " ppsjitter");
#endif
#ifdef STA_PPSWANDER
	if (status & STA_PPSWANDER) (void)fprintf(fp, " ppswander");
#endif
#ifdef STA_PPSERROR
	if (status & STA_PPSERROR) (void)fprintf(fp, " ppserror");
#endif
#ifdef STA_CLOCKERR
	if (status & STA_CLOCKERR) (void)fprintf(fp, " clockerr");
#endif
#ifdef STA_NANO
	if (status & STA_NANO) (void)fprintf(fp, " nano");
#endif
#ifdef STA_MODE
	if (status & STA_MODE) (void)fprintf(fp, " mode=fll");
#endif
#ifdef STA_CLK
	if (status & STA_CLK) (void)fprintf(fp, " src=B");
#endif
	(void)fprintf(fp, "\n");
	(void)fprintf(fp, "pll time constant:    %ld\n",
	    (u_long)ntohl(ik->constant));
	(void)fprintf(fp, "precision:            %g s\n",
	    (u_long)ntohl(ik->precision) * tscale_usec);
	(void)fprintf(fp, "frequency tolerance:  %s ppm\n",
	    fptoa((s_fp)ntohl(ik->tolerance), 0));

	/*
	 * For backwards compatibility (ugh), we find the pps variables
	 * only if the shift member is nonzero.
	 */
	if (!ik->shift)
	    return;

	/*
	 * pps variables
	 */
	(void)fprintf(fp, "pps frequency:        %s ppm\n",
	    fptoa((s_fp)ntohl(ik->ppsfreq), 3));
	(void)fprintf(fp, "pps stability:        %s ppm\n",
	    fptoa((s_fp)ntohl(ik->stabil), 3));
	(void)fprintf(fp, "pps jitter:           %g s\n",
	    (u_long)ntohl(ik->jitter) * tscale_unano);
	(void)fprintf(fp, "calibration interval: %d s\n",
		      1 << ntohs(ik->shift));
	(void)fprintf(fp, "calibration cycles:   %ld\n",
		      (u_long)ntohl(ik->calcnt));
	(void)fprintf(fp, "jitter exceeded:      %ld\n",
		      (u_long)ntohl(ik->jitcnt));
	(void)fprintf(fp, "stability exceeded:   %ld\n",
		      (u_long)ntohl(ik->stbcnt));
	(void)fprintf(fp, "calibration errors:   %ld\n",
		      (u_long)ntohl(ik->errcnt));
}

#define IF_LIST_FMT     "%2d %c %48s %c %c %12.12s %03lx %3lu %2lu %5lu %5lu %5lu %2lu %3lu %7lu\n"
#define IF_LIST_FMT_STR "%2s %c %48s %c %c %12.12s %3s %3s %2s %5s %5s %5s %2s %3s %7s\n"
#define IF_LIST_AFMT_STR "     %48s %c\n"
#define IF_LIST_LABELS  "#", 'A', "Address/Mask/Broadcast", 'T', 'E', "IF name", "Flg", "TL", "#M", "recv", "sent", "drop", "S", "PC", "uptime"
#define IF_LIST_LINE    "==================================================================================================================\n"

static void
iflist(
	FILE *fp,
	struct info_if_stats *ifs,
	size_t items,
	size_t itemsize,
	int res
	)
{
	static const char *actions = "?.+-";
	sockaddr_u saddr;

	if (res != 0)
	    return;

	if (!checkitems(items, fp))
	    return;

	if (!checkitemsize(itemsize, sizeof(struct info_if_stats)))
	    return;

	fprintf(fp, IF_LIST_FMT_STR, IF_LIST_LABELS);
	fprintf(fp, IF_LIST_LINE);
	
	while (items > 0) {
		SET_ADDR(saddr, ntohl(ifs->v6_flag), 
			 ifs->unaddr.addr.s_addr, ifs->unaddr.addr6);
		fprintf(fp, IF_LIST_FMT,
			ntohl(ifs->ifnum),
			actions[(ifs->action >= 1 && ifs->action < 4) ? ifs->action : 0],
			stoa((&saddr)), 'A',
			ifs->ignore_packets ? 'D' : 'E',
			ifs->name,
			(u_long)ntohl(ifs->flags),
			(u_long)ntohl(ifs->last_ttl),
			(u_long)ntohl(ifs->num_mcast),
			(u_long)ntohl(ifs->received),
			(u_long)ntohl(ifs->sent),
			(u_long)ntohl(ifs->notsent),
			(u_long)ntohl(ifs->scopeid),
			(u_long)ntohl(ifs->peercnt),
			(u_long)ntohl(ifs->uptime));

		SET_ADDR(saddr, ntohl(ifs->v6_flag), 
			 ifs->unmask.addr.s_addr, ifs->unmask.addr6);
		fprintf(fp, IF_LIST_AFMT_STR, stoa(&saddr), 'M');

		if (!ntohl(ifs->v6_flag) && ntohl(ifs->flags) & (INT_BCASTOPEN)) {
			SET_ADDR(saddr, ntohl(ifs->v6_flag), 
				 ifs->unbcast.addr.s_addr, ifs->unbcast.addr6);
			fprintf(fp, IF_LIST_AFMT_STR, stoa(&saddr), 'B');

		}

		ifs++;
		items--;
	}
}

/*ARGSUSED*/
static void
get_if_stats(
	struct parse *pcmd,
	FILE *fp
	)
{
	struct info_if_stats *ifs;
	size_t items;
	size_t itemsize;
	int res;

	res = doquery(impl_ver, REQ_IF_STATS, 1, 0, 0, (char *)NULL, &items,
		      &itemsize, (void *)&ifs, 0, 
		      sizeof(struct info_if_stats));
	iflist(fp, ifs, items, itemsize, res);
}

/*ARGSUSED*/
static void
do_if_reload(
	struct parse *pcmd,
	FILE *fp
	)
{
	struct info_if_stats *ifs;
	size_t items;
	size_t itemsize;
	int res;

	res = doquery(impl_ver, REQ_IF_RELOAD, 1, 0, 0, (char *)NULL, &items,
		      &itemsize, (void *)&ifs, 0, 
		      sizeof(struct info_if_stats));
	iflist(fp, ifs, items, itemsize, res);
}
