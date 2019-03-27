/*
 * ntpq-subs.c - subroutines which are called to perform ntpq commands.
 */
#include <config.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/time.h>

#include "ntpq.h"
#include "ntpq-opts.h"

extern char	currenthost[];
extern int	currenthostisnum;
size_t		maxhostlen;

/*
 * Declarations for command handlers in here
 */
static	associd_t checkassocid	(u_int32);
static	struct varlist *findlistvar (struct varlist *, char *);
static	void	doaddvlist	(struct varlist *, const char *);
static	void	dormvlist	(struct varlist *, const char *);
static	void	doclearvlist	(struct varlist *);
static	void	makequerydata	(struct varlist *, size_t *, char *);
static	int	doquerylist	(struct varlist *, int, associd_t, int,
				 u_short *, size_t *, const char **);
static	void	doprintvlist	(struct varlist *, FILE *);
static	void	addvars 	(struct parse *, FILE *);
static	void	rmvars		(struct parse *, FILE *);
static	void	clearvars	(struct parse *, FILE *);
static	void	showvars	(struct parse *, FILE *);
static	int	dolist		(struct varlist *, associd_t, int, int,
				 FILE *);
static	void	readlist	(struct parse *, FILE *);
static	void	writelist	(struct parse *, FILE *);
static	void	readvar 	(struct parse *, FILE *);
static	void	writevar	(struct parse *, FILE *);
static	void	clocklist	(struct parse *, FILE *);
static	void	clockvar	(struct parse *, FILE *);
static	int	findassidrange	(u_int32, u_int32, int *, int *,
				 FILE *);
static	void	mreadlist	(struct parse *, FILE *);
static	void	mreadvar	(struct parse *, FILE *);
static	void	printassoc	(int, FILE *);
static	void	associations	(struct parse *, FILE *);
static	void	lassociations	(struct parse *, FILE *);
static	void	passociations	(struct parse *, FILE *);
static	void	lpassociations	(struct parse *, FILE *);

#ifdef	UNUSED
static	void	radiostatus (struct parse *, FILE *);
#endif	/* UNUSED */

static	void	authinfo	(struct parse *, FILE *);
static	void	pstats	 	(struct parse *, FILE *);
static	long	when		(l_fp *, l_fp *, l_fp *);
static	char *	prettyinterval	(char *, size_t, long);
static	int	doprintpeers	(struct varlist *, int, int, size_t, const char *, FILE *, int);
static	int	dogetpeers	(struct varlist *, associd_t, FILE *, int);
static	void	dopeers 	(int, FILE *, int);
static	void	peers		(struct parse *, FILE *);
static	void	doapeers 	(int, FILE *, int);
static	void	apeers		(struct parse *, FILE *);
static	void	lpeers		(struct parse *, FILE *);
static	void	doopeers	(int, FILE *, int);
static	void	opeers		(struct parse *, FILE *);
static	void	lopeers 	(struct parse *, FILE *);
static	void	config		(struct parse *, FILE *);
static	void	saveconfig	(struct parse *, FILE *);
static	void	config_from_file(struct parse *, FILE *);
static	void	mrulist		(struct parse *, FILE *);
static	void	ifstats		(struct parse *, FILE *);
static	void	reslist		(struct parse *, FILE *);
static	void	sysstats	(struct parse *, FILE *);
static	void	sysinfo		(struct parse *, FILE *);
static	void	kerninfo	(struct parse *, FILE *);
static	void	monstats	(struct parse *, FILE *);
static	void	iostats		(struct parse *, FILE *);
static	void	timerstats	(struct parse *, FILE *);

/*
 * Commands we understand.	Ntpdc imports this.
 */
struct xcmd opcmds[] = {
	{ "saveconfig", saveconfig, { NTP_STR, NO, NO, NO },
		{ "filename", "", "", ""}, 
		"save ntpd configuration to file, . for current config file"},
	{ "associations", associations, {  NO, NO, NO, NO },
	  { "", "", "", "" },
	  "print list of association ID's and statuses for the server's peers" },
	{ "passociations", passociations,   {  NO, NO, NO, NO },
	  { "", "", "", "" },
	  "print list of associations returned by last associations command" },
	{ "lassociations", lassociations,   {  NO, NO, NO, NO },
	  { "", "", "", "" },
	  "print list of associations including all client information" },
	{ "lpassociations", lpassociations, {  NO, NO, NO, NO },
	  { "", "", "", "" },
	  "print last obtained list of associations, including client information" },
	{ "addvars",    addvars,    { NTP_STR, NO, NO, NO },
	  { "name[=value][,...]", "", "", "" },
	  "add variables to the variable list or change their values" },
	{ "rmvars", rmvars,     { NTP_STR, NO, NO, NO },
	  { "name[,...]", "", "", "" },
	  "remove variables from the variable list" },
	{ "clearvars",  clearvars,  { NO, NO, NO, NO },
	  { "", "", "", "" },
	  "remove all variables from the variable list" },
	{ "showvars",   showvars,   { NO, NO, NO, NO },
	  { "", "", "", "" },
	  "print variables on the variable list" },
	{ "readlist",   readlist,   { OPT|NTP_UINT, NO, NO, NO },
	  { "assocID", "", "", "" },
	  "read the system or peer variables included in the variable list" },
	{ "rl",     readlist,   { OPT|NTP_UINT, NO, NO, NO },
	  { "assocID", "", "", "" },
	  "read the system or peer variables included in the variable list" },
	{ "writelist",  writelist,  { OPT|NTP_UINT, NO, NO, NO },
	  { "assocID", "", "", "" },
	  "write the system or peer variables included in the variable list" },
	{ "readvar", readvar,    { OPT|NTP_UINT, OPT|NTP_STR, OPT|NTP_STR, OPT|NTP_STR, },
	  { "assocID", "varname1", "varname2", "varname3" },
	  "read system or peer variables" },
	{ "rv",      readvar,    { OPT|NTP_UINT, OPT|NTP_STR, OPT|NTP_STR, OPT|NTP_STR, },
	  { "assocID", "varname1", "varname2", "varname3" },
	  "read system or peer variables" },
	{ "writevar",   writevar,   { NTP_UINT, NTP_STR, NO, NO },
	  { "assocID", "name=value,[...]", "", "" },
	  "write system or peer variables" },
	{ "mreadlist",  mreadlist,  { NTP_UINT, NTP_UINT, NO, NO },
	  { "assocIDlow", "assocIDhigh", "", "" },
	  "read the peer variables in the variable list for multiple peers" },
	{ "mrl",    mreadlist,  { NTP_UINT, NTP_UINT, NO, NO },
	  { "assocIDlow", "assocIDhigh", "", "" },
	  "read the peer variables in the variable list for multiple peers" },
	{ "mreadvar",   mreadvar,   { NTP_UINT, NTP_UINT, OPT|NTP_STR, NO },
	  { "assocIDlow", "assocIDhigh", "name=value[,...]", "" },
	  "read peer variables from multiple peers" },
	{ "mrv",    mreadvar,   { NTP_UINT, NTP_UINT, OPT|NTP_STR, NO },
	  { "assocIDlow", "assocIDhigh", "name=value[,...]", "" },
	  "read peer variables from multiple peers" },
	{ "clocklist",  clocklist,  { OPT|NTP_UINT, NO, NO, NO },
	  { "assocID", "", "", "" },
	  "read the clock variables included in the variable list" },
	{ "cl",     clocklist,  { OPT|NTP_UINT, NO, NO, NO },
	  { "assocID", "", "", "" },
	  "read the clock variables included in the variable list" },
	{ "clockvar",   clockvar,   { OPT|NTP_UINT, OPT|NTP_STR, NO, NO },
	  { "assocID", "name=value[,...]", "", "" },
	  "read clock variables" },
	{ "cv",     clockvar,   { OPT|NTP_UINT, OPT|NTP_STR, NO, NO },
	  { "assocID", "name=value[,...]", "", "" },
	  "read clock variables" },
	{ "pstats",    pstats,    { NTP_UINT, NO, NO, NO },
	  { "assocID", "", "", "" },
	  "show statistics for a peer" },
	{ "peers",  peers,      { OPT|IP_VERSION, NO, NO, NO },
	  { "-4|-6", "", "", "" },
	  "obtain and print a list of the server's peers [IP version]" },
	{ "apeers",  apeers,      { OPT|IP_VERSION, NO, NO, NO },
	  { "-4|-6", "", "", "" },
	  "obtain and print a list of the server's peers and their assocIDs [IP version]" },
	{ "lpeers", lpeers,     { OPT|IP_VERSION, NO, NO, NO },
	  { "-4|-6", "", "", "" },
	  "obtain and print a list of all peers and clients [IP version]" },
	{ "opeers", opeers,     { OPT|IP_VERSION, NO, NO, NO },
	  { "-4|-6", "", "", "" },
	  "print peer list the old way, with dstadr shown rather than refid [IP version]" },
	{ "lopeers", lopeers,   { OPT|IP_VERSION, NO, NO, NO },
	  { "-4|-6", "", "", "" },
	  "obtain and print a list of all peers and clients showing dstadr [IP version]" },
	{ ":config", config,   { NTP_STR, NO, NO, NO },
	  { "<configuration command line>", "", "", "" },
	  "send a remote configuration command to ntpd" },
	{ "config-from-file", config_from_file, { NTP_STR, NO, NO, NO },
	  { "<configuration filename>", "", "", "" },
	  "configure ntpd using the configuration filename" },
	{ "mrulist", mrulist, { OPT|NTP_STR, OPT|NTP_STR, OPT|NTP_STR, OPT|NTP_STR },
	  { "tag=value", "tag=value", "tag=value", "tag=value" },
	  "display the list of most recently seen source addresses, tags mincount=... resall=0x... resany=0x..." },
	{ "ifstats", ifstats, { NO, NO, NO, NO },
	  { "", "", "", "" },
	  "show statistics for each local address ntpd is using" },
	{ "reslist", reslist, { NO, NO, NO, NO },
	  { "", "", "", "" },
	  "show ntpd access control list" },
	{ "sysinfo", sysinfo, { NO, NO, NO, NO },
	  { "", "", "", "" },
	  "display system summary" },
	{ "kerninfo", kerninfo, { NO, NO, NO, NO },
	  { "", "", "", "" },
	  "display kernel loop and PPS statistics" },
	{ "sysstats", sysstats, { NO, NO, NO, NO },
	  { "", "", "", "" },
	  "display system uptime and packet counts" },
	{ "monstats", monstats, { NO, NO, NO, NO },
	  { "", "", "", "" },
	  "display monitor (mrulist) counters and limits" },
	{ "authinfo", authinfo, { NO, NO, NO, NO },
	  { "", "", "", "" },
	  "display symmetric authentication counters" },
	{ "iostats", iostats, { NO, NO, NO, NO },
	  { "", "", "", "" },
	  "display network input and output counters" },
	{ "timerstats", timerstats, { NO, NO, NO, NO },
	  { "", "", "", "" },
	  "display interval timer counters" },
	{ 0,		0,		{ NO, NO, NO, NO },
	  { "-4|-6", "", "", "" }, "" }
};


/*
 * Variable list data space
 */
#define MAXLINE		512	/* maximum length of a line */
#define MAXLIST		128	/* maximum variables in list */
#define LENHOSTNAME	256	/* host name limit */

#define MRU_GOT_COUNT	0x1
#define MRU_GOT_LAST	0x2
#define MRU_GOT_FIRST	0x4
#define MRU_GOT_MV	0x8
#define MRU_GOT_RS	0x10
#define MRU_GOT_ADDR	0x20
#define MRU_GOT_ALL	(MRU_GOT_COUNT | MRU_GOT_LAST | MRU_GOT_FIRST \
			 | MRU_GOT_MV | MRU_GOT_RS | MRU_GOT_ADDR)

/*
 * mrulist() depends on MRUSORT_DEF and MRUSORT_RDEF being the first two
 */
typedef enum mru_sort_order_tag {
	MRUSORT_DEF = 0,	/* lstint ascending */
	MRUSORT_R_DEF,		/* lstint descending */
	MRUSORT_AVGINT,		/* avgint ascending */
	MRUSORT_R_AVGINT,	/* avgint descending */
	MRUSORT_ADDR,		/* IPv4 asc. then IPv6 asc. */
	MRUSORT_R_ADDR,		/* IPv6 desc. then IPv4 desc. */
	MRUSORT_COUNT,		/* hit count ascending */
	MRUSORT_R_COUNT,	/* hit count descending */
	MRUSORT_MAX,		/* special: count of this enum */
} mru_sort_order;

const char * const mru_sort_keywords[MRUSORT_MAX] = {
	"lstint",		/* MRUSORT_DEF */
	"-lstint",		/* MRUSORT_R_DEF */
	"avgint",		/* MRUSORT_AVGINT */
	"-avgint",		/* MRUSORT_R_AVGINT */
	"addr",			/* MRUSORT_ADDR */
	"-addr",		/* MRUSORT_R_ADDR */
	"count",		/* MRUSORT_COUNT */
	"-count",		/* MRUSORT_R_COUNT */
};

typedef int (*qsort_cmp)(const void *, const void *);

/*
 * Old CTL_PST defines for version 2.
 */
#define OLD_CTL_PST_CONFIG		0x80
#define OLD_CTL_PST_AUTHENABLE		0x40
#define OLD_CTL_PST_AUTHENTIC		0x20
#define OLD_CTL_PST_REACH		0x10
#define OLD_CTL_PST_SANE		0x08
#define OLD_CTL_PST_DISP		0x04

#define OLD_CTL_PST_SEL_REJECT		0
#define OLD_CTL_PST_SEL_SELCAND 	1
#define OLD_CTL_PST_SEL_SYNCCAND	2
#define OLD_CTL_PST_SEL_SYSPEER 	3

char flash2[] = " .+*    "; /* flash decode for version 2 */
char flash3[] = " x.-+#*o"; /* flash decode for peer status version 3 */

struct varlist {
	const char *name;
	char *value;
} g_varlist[MAXLIST] = { { 0, 0 } };

/*
 * Imported from ntpq.c
 */
extern int showhostnames;
extern int wideremote;
extern int rawmode;
extern struct servent *server_entry;
extern struct association *assoc_cache;
extern u_char pktversion;

typedef struct mru_tag mru;
struct mru_tag {
	mru *		hlink;	/* next in hash table bucket */
	DECL_DLIST_LINK(mru, mlink);
	int		count;
	l_fp		last;
	l_fp		first;
	u_char		mode;
	u_char		ver;
	u_short		rs;
	sockaddr_u	addr;
};

typedef struct ifstats_row_tag {
	u_int		ifnum;
	sockaddr_u	addr;
	sockaddr_u	bcast;
	int		enabled;
	u_int		flags;
	u_int		mcast_count;
	char		name[32];
	u_int		peer_count;
	u_int		received;
	u_int		sent;
	u_int		send_errors;
	u_int		ttl;
	u_int		uptime;
} ifstats_row;

typedef struct reslist_row_tag {
	u_int		idx;
	sockaddr_u	addr;
	sockaddr_u	mask;
	u_long		hits;
	char		flagstr[128];
} reslist_row;

typedef struct var_display_collection_tag {
	const char * const tag;		/* system variable */
	const char * const display;	/* descriptive text */
	u_char type;			/* NTP_STR, etc */
	union {
		char *		str;
		sockaddr_u	sau;	/* NTP_ADD */
		l_fp		lfp;	/* NTP_LFP */
	} v;				/* retrieved value */
} vdc;
#if !defined(MISSING_C99_STRUCT_INIT)
# define VDC_INIT(a, b, c) { .tag = a, .display = b, .type = c }
#else
# define VDC_INIT(a, b, c) { a, b, c }
#endif
/*
 * other local function prototypes
 */
static int	mrulist_ctrl_c_hook(void);
static mru *	add_mru(mru *);
static int	collect_mru_list(const char *, l_fp *);
static int	fetch_nonce(char *, size_t);
static int	qcmp_mru_avgint(const void *, const void *);
static int	qcmp_mru_r_avgint(const void *, const void *);
static int	qcmp_mru_addr(const void *, const void *);
static int	qcmp_mru_r_addr(const void *, const void *);
static int	qcmp_mru_count(const void *, const void *);
static int	qcmp_mru_r_count(const void *, const void *);
static void	validate_ifnum(FILE *, u_int, int *, ifstats_row *);
static void	another_ifstats_field(int *, ifstats_row *, FILE *);
static void	collect_display_vdc(associd_t as, vdc *table,
				    int decodestatus, FILE *fp);

/*
 * static globals
 */
static u_int	mru_count;
static u_int	mru_dupes;
volatile int	mrulist_interrupted;
static mru	mru_list;		/* listhead */
static mru **	hash_table;

/*
 * qsort comparison function table for mrulist().  The first two
 * entries are NULL because they are handled without qsort().
 */
static const qsort_cmp mru_qcmp_table[MRUSORT_MAX] = {
	NULL,			/* MRUSORT_DEF unused */
	NULL,			/* MRUSORT_R_DEF unused */
	&qcmp_mru_avgint,	/* MRUSORT_AVGINT */
	&qcmp_mru_r_avgint,	/* MRUSORT_R_AVGINT */
	&qcmp_mru_addr,		/* MRUSORT_ADDR */
	&qcmp_mru_r_addr,	/* MRUSORT_R_ADDR */
	&qcmp_mru_count,	/* MRUSORT_COUNT */
	&qcmp_mru_r_count,	/* MRUSORT_R_COUNT */
};

/*
 * checkassocid - return the association ID, checking to see if it is valid
 */
static associd_t
checkassocid(
	u_int32 value
	)
{
	associd_t	associd;
	u_long		ulvalue;

	associd = (associd_t)value;
	if (0 == associd || value != associd) {
		ulvalue = value;
		fprintf(stderr,
			"***Invalid association ID %lu specified\n",
			ulvalue);
		return 0;
	}

	return associd;
}


/*
 * findlistvar - Look for the named variable in a varlist.  If found,
 *		 return a pointer to it.  Otherwise, if the list has
 *		 slots available, return the pointer to the first free
 *		 slot, or NULL if it's full.
 */
static struct varlist *
findlistvar(
	struct varlist *list,
	char *name
	)
{
	struct varlist *vl;

	for (vl = list; vl < list + MAXLIST && vl->name != NULL; vl++)
		if (!strcmp(name, vl->name))
			return vl;
	if (vl < list + MAXLIST)
		return vl;

	return NULL;
}


/*
 * doaddvlist - add variable(s) to the variable list
 */
static void
doaddvlist(
	struct varlist *vlist,
	const char *vars
	)
{
	struct varlist *vl;
	size_t len;
	char *name;
	char *value;

	len = strlen(vars);
	while (nextvar(&len, &vars, &name, &value)) {
		INSIST(name && value);
		vl = findlistvar(vlist, name);
		if (NULL == vl) {
			fprintf(stderr, "Variable list full\n");
			return;
		}

		if (NULL == vl->name) {
			vl->name = estrdup(name);
		} else if (vl->value != NULL) {
			free(vl->value);
			vl->value = NULL;
		}

		if (value != NULL)
			vl->value = estrdup(value);
	}
}


/*
 * dormvlist - remove variable(s) from the variable list
 */
static void
dormvlist(
	struct varlist *vlist,
	const char *vars
	)
{
	struct varlist *vl;
	size_t len;
	char *name;
	char *value;

	len = strlen(vars);
	while (nextvar(&len, &vars, &name, &value)) {
		INSIST(name && value);
		vl = findlistvar(vlist, name);
		if (vl == 0 || vl->name == 0) {
			(void) fprintf(stderr, "Variable `%s' not found\n",
				       name);
		} else {
			free((void *)(intptr_t)vl->name);
			if (vl->value != 0)
			    free(vl->value);
			for ( ; (vl+1) < (g_varlist + MAXLIST)
				      && (vl+1)->name != 0; vl++) {
				vl->name = (vl+1)->name;
				vl->value = (vl+1)->value;
			}
			vl->name = vl->value = 0;
		}
	}
}


/*
 * doclearvlist - clear a variable list
 */
static void
doclearvlist(
	struct varlist *vlist
	)
{
	register struct varlist *vl;

	for (vl = vlist; vl < vlist + MAXLIST && vl->name != 0; vl++) {
		free((void *)(intptr_t)vl->name);
		vl->name = 0;
		if (vl->value != 0) {
			free(vl->value);
			vl->value = 0;
		}
	}
}


/*
 * makequerydata - form a data buffer to be included with a query
 */
static void
makequerydata(
	struct varlist *vlist,
	size_t *datalen,
	char *data
	)
{
	register struct varlist *vl;
	register char *cp, *cpend;
	register size_t namelen, valuelen;
	register size_t totallen;

	cp = data;
	cpend = data + *datalen;

	for (vl = vlist; vl < vlist + MAXLIST && vl->name != 0; vl++) {
		namelen = strlen(vl->name);
		if (vl->value == 0)
			valuelen = 0;
		else
			valuelen = strlen(vl->value);
		totallen = namelen + valuelen + (valuelen != 0) + (cp != data);
		if (cp + totallen > cpend) {
		    fprintf(stderr, 
			    "***Ignoring variables starting with `%s'\n",
			    vl->name);
		    break;
		}

		if (cp != data)
			*cp++ = ',';
		memcpy(cp, vl->name, (size_t)namelen);
		cp += namelen;
		if (valuelen != 0) {
			*cp++ = '=';
			memcpy(cp, vl->value, (size_t)valuelen);
			cp += valuelen;
		}
	}
	*datalen = (size_t)(cp - data);
}


/*
 * doquerylist - send a message including variables in a list
 */
static int
doquerylist(
	struct varlist *vlist,
	int op,
	associd_t associd,
	int auth,
	u_short *rstatus,
	size_t *dsize,
	const char **datap
	)
{
	char data[CTL_MAX_DATA_LEN];
	size_t datalen;

	datalen = sizeof(data);
	makequerydata(vlist, &datalen, data);

	return doquery(op, associd, auth, datalen, data, rstatus, dsize,
		       datap);
}


/*
 * doprintvlist - print the variables on a list
 */
static void
doprintvlist(
	struct varlist *vlist,
	FILE *fp
	)
{
	size_t n;

	if (NULL == vlist->name) {
		fprintf(fp, "No variables on list\n");
		return;
	}
	for (n = 0; n < MAXLIST && vlist[n].name != NULL; n++) {
		if (NULL == vlist[n].value)
			fprintf(fp, "%s\n", vlist[n].name);
		else
			fprintf(fp, "%s=%s\n", vlist[n].name,
				vlist[n].value);
	}
}

/*
 * addvars - add variables to the variable list
 */
/*ARGSUSED*/
static void
addvars(
	struct parse *pcmd,
	FILE *fp
	)
{
	doaddvlist(g_varlist, pcmd->argval[0].string);
}


/*
 * rmvars - remove variables from the variable list
 */
/*ARGSUSED*/
static void
rmvars(
	struct parse *pcmd,
	FILE *fp
	)
{
	dormvlist(g_varlist, pcmd->argval[0].string);
}


/*
 * clearvars - clear the variable list
 */
/*ARGSUSED*/
static void
clearvars(
	struct parse *pcmd,
	FILE *fp
	)
{
	doclearvlist(g_varlist);
}


/*
 * showvars - show variables on the variable list
 */
/*ARGSUSED*/
static void
showvars(
	struct parse *pcmd,
	FILE *fp
	)
{
	doprintvlist(g_varlist, fp);
}


/*
 * dolist - send a request with the given list of variables
 */
static int
dolist(
	struct varlist *vlist,
	associd_t associd,
	int op,
	int type,
	FILE *fp
	)
{
	const char *datap;
	int res;
	size_t dsize;
	u_short rstatus;
	int quiet;

	/*
	 * if we're asking for specific variables don't include the
	 * status header line in the output.
	 */
	if (old_rv)
		quiet = 0;
	else
		quiet = (vlist->name != NULL);

	res = doquerylist(vlist, op, associd, 0, &rstatus, &dsize, &datap);

	if (res != 0)
		return 0;

	if (numhosts > 1)
		fprintf(fp, "server=%s ", currenthost);
	if (dsize == 0) {
		if (associd == 0)
			fprintf(fp, "No system%s variables returned\n",
				(type == TYPE_CLOCK) ? " clock" : "");
		else
			fprintf(fp,
				"No information returned for%s association %u\n",
				(type == TYPE_CLOCK) ? " clock" : "",
				associd);
		return 1;
	}

	if (!quiet)
		fprintf(fp, "associd=%u ", associd);
	printvars(dsize, datap, (int)rstatus, type, quiet, fp);
	return 1;
}


/*
 * readlist - send a read variables request with the variables on the list
 */
static void
readlist(
	struct parse *pcmd,
	FILE *fp
	)
{
	associd_t	associd;
	int		type;

	if (pcmd->nargs == 0) {
		associd = 0;
	} else {
	  /* HMS: I think we want the u_int32 target here, not the u_long */
		if (pcmd->argval[0].uval == 0)
			associd = 0;
		else if ((associd = checkassocid(pcmd->argval[0].uval)) == 0)
			return;
	}

	type = (0 == associd)
		   ? TYPE_SYS
		   : TYPE_PEER;
	dolist(g_varlist, associd, CTL_OP_READVAR, type, fp);
}


/*
 * writelist - send a write variables request with the variables on the list
 */
static void
writelist(
	struct parse *pcmd,
	FILE *fp
	)
{
	const char *datap;
	int res;
	associd_t associd;
	size_t dsize;
	u_short rstatus;

	if (pcmd->nargs == 0) {
		associd = 0;
	} else {
		/* HMS: Do we really want uval here? */
		if (pcmd->argval[0].uval == 0)
			associd = 0;
		else if ((associd = checkassocid(pcmd->argval[0].uval)) == 0)
			return;
	}

	res = doquerylist(g_varlist, CTL_OP_WRITEVAR, associd, 1, &rstatus,
			  &dsize, &datap);

	if (res != 0)
		return;

	if (numhosts > 1)
		(void) fprintf(fp, "server=%s ", currenthost);
	if (dsize == 0)
		(void) fprintf(fp, "done! (no data returned)\n");
	else {
		(void) fprintf(fp,"associd=%u ", associd);
		printvars(dsize, datap, (int)rstatus,
			  (associd != 0) ? TYPE_PEER : TYPE_SYS, 0, fp);
	}
	return;
}


/*
 * readvar - send a read variables request with the specified variables
 */
static void
readvar(
	struct parse *pcmd,
	FILE *fp
	)
{
	associd_t	associd;
	size_t		tmpcount;
	size_t		u;
	int		type;
	struct varlist	tmplist[MAXLIST];


	/* HMS: uval? */
	if (pcmd->nargs == 0 || pcmd->argval[0].uval == 0)
		associd = 0;
	else if ((associd = checkassocid(pcmd->argval[0].uval)) == 0)
		return;

	ZERO(tmplist);
	if (pcmd->nargs > 1) {
		tmpcount = pcmd->nargs - 1;
		for (u = 0; u < tmpcount; u++)
			doaddvlist(tmplist, pcmd->argval[1 + u].string);
	}

	type = (0 == associd)
		   ? TYPE_SYS
		   : TYPE_PEER;
	dolist(tmplist, associd, CTL_OP_READVAR, type, fp);

	doclearvlist(tmplist);
}


/*
 * writevar - send a write variables request with the specified variables
 */
static void
writevar(
	struct parse *pcmd,
	FILE *fp
	)
{
	const char *datap;
	int res;
	associd_t associd;
	int type;
	size_t dsize;
	u_short rstatus;
	struct varlist tmplist[MAXLIST];

	/* HMS: uval? */
	if (pcmd->argval[0].uval == 0)
		associd = 0;
	else if ((associd = checkassocid(pcmd->argval[0].uval)) == 0)
		return;

	ZERO(tmplist);
	doaddvlist(tmplist, pcmd->argval[1].string);

	res = doquerylist(tmplist, CTL_OP_WRITEVAR, associd, 1, &rstatus,
			  &dsize, &datap);

	doclearvlist(tmplist);

	if (res != 0)
		return;

	if (numhosts > 1)
		fprintf(fp, "server=%s ", currenthost);
	if (dsize == 0)
		fprintf(fp, "done! (no data returned)\n");
	else {
		fprintf(fp,"associd=%u ", associd);
		type = (0 == associd)
			   ? TYPE_SYS
			   : TYPE_PEER;
		printvars(dsize, datap, (int)rstatus, type, 0, fp);
	}
	return;
}


/*
 * clocklist - send a clock variables request with the variables on the list
 */
static void
clocklist(
	struct parse *pcmd,
	FILE *fp
	)
{
	associd_t associd;

	/* HMS: uval? */
	if (pcmd->nargs == 0) {
		associd = 0;
	} else {
		if (pcmd->argval[0].uval == 0)
			associd = 0;
		else if ((associd = checkassocid(pcmd->argval[0].uval)) == 0)
			return;
	}

	dolist(g_varlist, associd, CTL_OP_READCLOCK, TYPE_CLOCK, fp);
}


/*
 * clockvar - send a clock variables request with the specified variables
 */
static void
clockvar(
	struct parse *pcmd,
	FILE *fp
	)
{
	associd_t associd;
	struct varlist tmplist[MAXLIST];

	/* HMS: uval? */
	if (pcmd->nargs == 0 || pcmd->argval[0].uval == 0)
		associd = 0;
	else if ((associd = checkassocid(pcmd->argval[0].uval)) == 0)
		return;

	ZERO(tmplist);
	if (pcmd->nargs >= 2)
		doaddvlist(tmplist, pcmd->argval[1].string);

	dolist(tmplist, associd, CTL_OP_READCLOCK, TYPE_CLOCK, fp);

	doclearvlist(tmplist);
}


/*
 * findassidrange - verify a range of association ID's
 */
static int
findassidrange(
	u_int32	assid1,
	u_int32	assid2,
	int *	from,
	int *	to,
	FILE *	fp
	)
{
	associd_t	assids[2];
	int		ind[COUNTOF(assids)];
	u_int		i;
	size_t		a;


	if (0 == numassoc)
		dogetassoc(fp);

	assids[0] = checkassocid(assid1);
	if (0 == assids[0])
		return 0;
	assids[1] = checkassocid(assid2);
	if (0 == assids[1])
		return 0;

	for (a = 0; a < COUNTOF(assids); a++) {
		ind[a] = -1;
		for (i = 0; i < numassoc; i++)
			if (assoc_cache[i].assid == assids[a])
				ind[a] = i;
	}
	for (a = 0; a < COUNTOF(assids); a++)
		if (-1 == ind[a]) {
			fprintf(stderr,
				"***Association ID %u not found in list\n",
				assids[a]);
			return 0;
		}

	if (ind[0] < ind[1]) {
		*from = ind[0];
		*to = ind[1];
	} else {
		*to = ind[0];
		*from = ind[1];
	}
	return 1;
}



/*
 * mreadlist - send a read variables request for multiple associations
 */
static void
mreadlist(
	struct parse *pcmd,
	FILE *fp
	)
{
	int i;
	int from;
	int to;

	if (!findassidrange(pcmd->argval[0].uval, pcmd->argval[1].uval,
			    &from, &to, fp))
		return;

	for (i = from; i <= to; i++) {
		if (i != from)
			fprintf(fp, "\n");
		if (!dolist(g_varlist, assoc_cache[i].assid,
			    CTL_OP_READVAR, TYPE_PEER, fp))
			return;
	}
	return;
}


/*
 * mreadvar - send a read variables request for multiple associations
 */
static void
mreadvar(
	struct parse *pcmd,
	FILE *fp
	)
{
	int i;
	int from;
	int to;
	struct varlist tmplist[MAXLIST];
	struct varlist *pvars;

	if (!findassidrange(pcmd->argval[0].uval, pcmd->argval[1].uval,
				&from, &to, fp))
		return;

	ZERO(tmplist);
	if (pcmd->nargs >= 3) {
		doaddvlist(tmplist, pcmd->argval[2].string);
		pvars = tmplist;
	} else {
		pvars = g_varlist;
	}

	for (i = from; i <= to; i++) {
		if (!dolist(pvars, assoc_cache[i].assid, CTL_OP_READVAR,
			    TYPE_PEER, fp))
			break;
	}

	if (pvars == tmplist)
		doclearvlist(tmplist);

	return;
}


/*
 * dogetassoc - query the host for its list of associations
 */
int
dogetassoc(
	FILE *fp
	)
{
	const char *datap;
	const u_short *pus;
	int res;
	size_t dsize;
	u_short rstatus;

	res = doquery(CTL_OP_READSTAT, 0, 0, 0, (char *)0, &rstatus,
			  &dsize, &datap);

	if (res != 0)
		return 0;

	if (dsize == 0) {
		if (numhosts > 1)
			fprintf(fp, "server=%s ", currenthost);
		fprintf(fp, "No association ID's returned\n");
		return 0;
	}

	if (dsize & 0x3) {
		if (numhosts > 1)
			fprintf(stderr, "server=%s ", currenthost);
		fprintf(stderr,
			"***Server returned %zu octets, should be multiple of 4\n",
			dsize);
		return 0;
	}

	numassoc = 0;

	while (dsize > 0) {
		if (numassoc >= assoc_cache_slots) {
			grow_assoc_cache();
		}
		pus = (const void *)datap;
		assoc_cache[numassoc].assid = ntohs(*pus);
		datap += sizeof(*pus);
		pus = (const void *)datap;
		assoc_cache[numassoc].status = ntohs(*pus);
		datap += sizeof(*pus);
		dsize -= 2 * sizeof(*pus);
		if (debug) {
			fprintf(stderr, "[%u] ",
				assoc_cache[numassoc].assid);
		}
		numassoc++;
	}
	if (debug) {
		fprintf(stderr, "\n%d associations total\n", numassoc);
	}
	sortassoc();
	return 1;
}


/*
 * printassoc - print the current list of associations
 */
static void
printassoc(
	int showall,
	FILE *fp
	)
{
	register char *bp;
	u_int i;
	u_char statval;
	int event;
	u_long event_count;
	const char *conf;
	const char *reach;
	const char *auth;
	const char *condition = "";
	const char *last_event;
	char buf[128];

	if (numassoc == 0) {
		(void) fprintf(fp, "No association ID's in list\n");
		return;
	}

	/*
	 * Output a header
	 */
	(void) fprintf(fp,
			   "ind assid status  conf reach auth condition  last_event cnt\n");
	(void) fprintf(fp,
			   "===========================================================\n");
	for (i = 0; i < numassoc; i++) {
		statval = (u_char) CTL_PEER_STATVAL(assoc_cache[i].status);
		if (!showall && !(statval & (CTL_PST_CONFIG|CTL_PST_REACH)))
			continue;
		event = CTL_PEER_EVENT(assoc_cache[i].status);
		event_count = CTL_PEER_NEVNT(assoc_cache[i].status);
		if (statval & CTL_PST_CONFIG)
			conf = "yes";
		else
			conf = "no";
		if (statval & CTL_PST_BCAST) {
			reach = "none";
			if (statval & CTL_PST_AUTHENABLE)
				auth = "yes";
			else
				auth = "none";
		} else {
			if (statval & CTL_PST_REACH)
				reach = "yes";
			else
				reach = "no";
			if (statval & CTL_PST_AUTHENABLE) {
				if (statval & CTL_PST_AUTHENTIC)
					auth = "ok ";
				else
					auth = "bad";
			} else {
				auth = "none";
			}
		}
		if (pktversion > NTP_OLDVERSION) {
			switch (statval & 0x7) {

			case CTL_PST_SEL_REJECT:
				condition = "reject";
				break;

			case CTL_PST_SEL_SANE:
				condition = "falsetick";
				break;

			case CTL_PST_SEL_CORRECT:
				condition = "excess";
				break;

			case CTL_PST_SEL_SELCAND:
				condition = "outlier";
				break;

			case CTL_PST_SEL_SYNCCAND:
				condition = "candidate";
				break;

			case CTL_PST_SEL_EXCESS:
				condition = "backup";
				break;

			case CTL_PST_SEL_SYSPEER:
				condition = "sys.peer";
				break;

			case CTL_PST_SEL_PPS:
				condition = "pps.peer";
				break;
			}
		} else {
			switch (statval & 0x3) {

			case OLD_CTL_PST_SEL_REJECT:
				if (!(statval & OLD_CTL_PST_SANE))
					condition = "insane";
				else if (!(statval & OLD_CTL_PST_DISP))
					condition = "hi_disp";
				else
					condition = "";
				break;

			case OLD_CTL_PST_SEL_SELCAND:
				condition = "sel_cand";
				break;

			case OLD_CTL_PST_SEL_SYNCCAND:
				condition = "sync_cand";
				break;

			case OLD_CTL_PST_SEL_SYSPEER:
				condition = "sys_peer";
				break;
			}
		}
		switch (PEER_EVENT|event) {

		case PEVNT_MOBIL:
			last_event = "mobilize";
			break;

		case PEVNT_DEMOBIL:
			last_event = "demobilize";
			break;

		case PEVNT_REACH:
			last_event = "reachable";
			break;

		case PEVNT_UNREACH:
			last_event = "unreachable";
			break;

		case PEVNT_RESTART:
			last_event = "restart";
			break;

		case PEVNT_REPLY:
			last_event = "no_reply";
			break;

		case PEVNT_RATE:
			last_event = "rate_exceeded";
			break;

		case PEVNT_DENY:
			last_event = "access_denied";
			break;

		case PEVNT_ARMED:
			last_event = "leap_armed";
			break;

		case PEVNT_NEWPEER:
			last_event = "sys_peer";
			break;

		case PEVNT_CLOCK:
			last_event = "clock_alarm";
			break;

		default:
			last_event = "";
			break;
		}
		snprintf(buf, sizeof(buf),
			 "%3d %5u  %04x   %3.3s  %4s  %4.4s %9.9s %11s %2lu",
			 i + 1, assoc_cache[i].assid,
			 assoc_cache[i].status, conf, reach, auth,
			 condition, last_event, event_count);
		bp = buf + strlen(buf);
		while (bp > buf && ' ' == bp[-1])
			--bp;
		bp[0] = '\0';
		fprintf(fp, "%s\n", buf);
	}
}


/*
 * associations - get, record and print a list of associations
 */
/*ARGSUSED*/
static void
associations(
	struct parse *pcmd,
	FILE *fp
	)
{
	if (dogetassoc(fp))
		printassoc(0, fp);
}


/*
 * lassociations - get, record and print a long list of associations
 */
/*ARGSUSED*/
static void
lassociations(
	struct parse *pcmd,
	FILE *fp
	)
{
	if (dogetassoc(fp))
		printassoc(1, fp);
}


/*
 * passociations - print the association list
 */
/*ARGSUSED*/
static void
passociations(
	struct parse *pcmd,
	FILE *fp
	)
{
	printassoc(0, fp);
}


/*
 * lpassociations - print the long association list
 */
/*ARGSUSED*/
static void
lpassociations(
	struct parse *pcmd,
	FILE *fp
	)
{
	printassoc(1, fp);
}


/*
 *  saveconfig - dump ntp server configuration to server file
 */
static void
saveconfig(
	struct parse *pcmd,
	FILE *fp
	)
{
	const char *datap;
	int res;
	size_t dsize;
	u_short rstatus;

	if (0 == pcmd->nargs)
		return;
	
	res = doquery(CTL_OP_SAVECONFIG, 0, 1,
		      strlen(pcmd->argval[0].string),
		      pcmd->argval[0].string, &rstatus, &dsize,
		      &datap);

	if (res != 0)
		return;

	if (0 == dsize)
		fprintf(fp, "(no response message, curiously)");
	else
		fprintf(fp, "%.*s", (int)dsize, datap); /* cast is wobbly */
}


#ifdef	UNUSED
/*
 * radiostatus - print the radio status returned by the server
 */
/*ARGSUSED*/
static void
radiostatus(
	struct parse *pcmd,
	FILE *fp
	)
{
	char *datap;
	int res;
	int dsize;
	u_short rstatus;

	res = doquery(CTL_OP_READCLOCK, 0, 0, 0, (char *)0, &rstatus,
			  &dsize, &datap);

	if (res != 0)
		return;

	if (numhosts > 1)
		(void) fprintf(fp, "server=%s ", currenthost);
	if (dsize == 0) {
		(void) fprintf(fp, "No radio status string returned\n");
		return;
	}

	asciize(dsize, datap, fp);
}
#endif	/* UNUSED */

/*
 * when - print how long its been since his last packet arrived
 */
static long
when(
	l_fp *ts,
	l_fp *rec,
	l_fp *reftime
	)
{
	l_fp *lasttime;

	if (rec->l_ui != 0)
		lasttime = rec;
	else if (reftime->l_ui != 0)
		lasttime = reftime;
	else
		return 0;

	if (ts->l_ui < lasttime->l_ui)
		return -1;
	return (ts->l_ui - lasttime->l_ui);
}


/*
 * Pretty-print an interval into the given buffer, in a human-friendly format.
 */
static char *
prettyinterval(
	char *buf,
	size_t cb,
	long diff
	)
{
	if (diff <= 0) {
		buf[0] = '-';
		buf[1] = 0;
		return buf;
	}

	if (diff <= 2048) {
		snprintf(buf, cb, "%u", (unsigned int)diff);
		return buf;
	}

	diff = (diff + 29) / 60;
	if (diff <= 300) {
		snprintf(buf, cb, "%um", (unsigned int)diff);
		return buf;
	}

	diff = (diff + 29) / 60;
	if (diff <= 96) {
		snprintf(buf, cb, "%uh", (unsigned int)diff);
		return buf;
	}

	diff = (diff + 11) / 24;
	if (diff <= 999) {
		snprintf(buf, cb, "%ud", (unsigned int)diff);
		return buf;
	}

	/* years are only approximated... */
	diff = (long)floor(diff / 365.25 + 0.5);
	if (diff <= 999) {
		snprintf(buf, cb, "%uy", (unsigned int)diff);
		return buf;
	}
	/* Ok, this amounts to infinity... */
	strlcpy(buf, "INF", cb);
	return buf;
}

static char
decodeaddrtype(
	sockaddr_u *sock
	)
{
	char ch = '-';
	u_int32 dummy;

	switch(AF(sock)) {
	case AF_INET:
		dummy = SRCADR(sock);
		ch = (char)(((dummy&0xf0000000)==0xe0000000) ? 'm' :
			((dummy&0x000000ff)==0x000000ff) ? 'b' :
			((dummy&0xffffffff)==0x7f000001) ? 'l' :
			((dummy&0xffffffe0)==0x00000000) ? '-' :
			'u');
		break;
	case AF_INET6:
		if (IN6_IS_ADDR_MULTICAST(PSOCK_ADDR6(sock)))
			ch = 'm';
		else
			ch = 'u';
		break;
	default:
		ch = '-';
		break;
	}
	return ch;
}

/*
 * A list of variables required by the peers command
 */
struct varlist opeervarlist[] = {
	{ "srcadr",	0 },	/* 0 */
	{ "dstadr",	0 },	/* 1 */
	{ "stratum",	0 },	/* 2 */
	{ "hpoll",	0 },	/* 3 */
	{ "ppoll",	0 },	/* 4 */
	{ "reach",	0 },	/* 5 */
	{ "delay",	0 },	/* 6 */
	{ "offset",	0 },	/* 7 */
	{ "jitter",	0 },	/* 8 */
	{ "dispersion", 0 },	/* 9 */
	{ "rec",	0 },	/* 10 */
	{ "reftime",	0 },	/* 11 */
	{ "srcport",	0 },	/* 12 */
	{ "hmode",	0 },	/* 13 */
	{ 0,		0 }
};

struct varlist peervarlist[] = {
	{ "srcadr",	0 },	/* 0 */
	{ "refid",	0 },	/* 1 */
	{ "stratum",	0 },	/* 2 */
	{ "hpoll",	0 },	/* 3 */
	{ "ppoll",	0 },	/* 4 */
	{ "reach",	0 },	/* 5 */
	{ "delay",	0 },	/* 6 */
	{ "offset",	0 },	/* 7 */
	{ "jitter",	0 },	/* 8 */
	{ "dispersion", 0 },	/* 9 */
	{ "rec",	0 },	/* 10 */
	{ "reftime",	0 },	/* 11 */
	{ "srcport",	0 },	/* 12 */
	{ "hmode",	0 },	/* 13 */
	{ "srchost",	0 },	/* 14 */
	{ 0,		0 }
};

struct varlist apeervarlist[] = {
	{ "srcadr",	0 },	/* 0 */
	{ "refid",	0 },	/* 1 */
	{ "assid",	0 },	/* 2 */
	{ "stratum",	0 },	/* 3 */
	{ "hpoll",	0 },	/* 4 */
	{ "ppoll",	0 },	/* 5 */
	{ "reach",	0 },	/* 6 */
	{ "delay",	0 },	/* 7 */
	{ "offset",	0 },	/* 8 */
	{ "jitter",	0 },	/* 9 */
	{ "dispersion", 0 },	/* 10 */
	{ "rec",	0 },	/* 11 */
	{ "reftime",	0 },	/* 12 */
	{ "srcport",	0 },	/* 13 */
	{ "hmode",	0 },	/* 14 */
	{ "srchost",	0 },	/* 15 */
	{ 0,		0 }
};


/*
 * Decode an incoming data buffer and print a line in the peer list
 */
static int
doprintpeers(
	struct varlist *pvl,
	int associd,
	int rstatus,
	size_t datalen,
	const char *data,
	FILE *fp,
	int af
	)
{
	char *name;
	char *value = NULL;
	int c;
	size_t len;
	int have_srchost;
	int have_dstadr;
	int have_da_rid;
	int have_jitter;
	sockaddr_u srcadr;
	sockaddr_u dstadr;
	sockaddr_u dum_store;
	sockaddr_u refidadr;
	long hmode = 0;
	u_long srcport = 0;
	u_int32 u32;
	const char *dstadr_refid = "0.0.0.0";
	const char *serverlocal;
	size_t drlen;
	u_long stratum = 0;
	long ppoll = 0;
	long hpoll = 0;
	u_long reach = 0;
	l_fp estoffset;
	l_fp estdelay;
	l_fp estjitter;
	l_fp estdisp;
	l_fp reftime;
	l_fp rec;
	l_fp ts;
	u_long poll_sec;
	u_long flash = 0;
	char type = '?';
	char clock_name[LENHOSTNAME];
	char whenbuf[12], pollbuf[12];
	/* [Bug 3482] formally whenbuf & pollbuf should be able to hold
	 * a full signed int. Not that we would use that much string
	 * data for it...
	 */
	get_systime(&ts);
	
	have_srchost = FALSE;
	have_dstadr = FALSE;
	have_da_rid = FALSE;
	have_jitter = FALSE;
	ZERO_SOCK(&srcadr);
	ZERO_SOCK(&dstadr);
	clock_name[0] = '\0';
	ZERO(estoffset);
	ZERO(estdelay);
	ZERO(estjitter);
	ZERO(estdisp);

	while (nextvar(&datalen, &data, &name, &value)) {
		INSIST(name && value);
		if (!strcmp("srcadr", name) ||
		    !strcmp("peeradr", name)) {
			if (!decodenetnum(value, &srcadr))
				fprintf(stderr, "malformed %s=%s\n",
					name, value);
		} else if (!strcmp("srchost", name)) {
			if (pvl == peervarlist || pvl == apeervarlist) {
				len = strlen(value);
				if (2 < len &&
				    (size_t)len < sizeof(clock_name)) {
					/* strip quotes */
					value++;
					len -= 2;
					memcpy(clock_name, value, len);
					clock_name[len] = '\0';
					have_srchost = TRUE;
				}
			}
		} else if (!strcmp("dstadr", name)) {
			if (decodenetnum(value, &dum_store)) {
				type = decodeaddrtype(&dum_store);
				have_dstadr = TRUE;
				dstadr = dum_store;
				if (pvl == opeervarlist) {
					have_da_rid = TRUE;
					dstadr_refid = trunc_left(stoa(&dstadr), 15);
				}
			}
		} else if (!strcmp("hmode", name)) {
			decodeint(value, &hmode);
		} else if (!strcmp("refid", name)) {
			if (   (pvl == peervarlist)
			    && (drefid == REFID_IPV4)) {
				have_da_rid = TRUE;
				drlen = strlen(value);
				if (0 == drlen) {
					dstadr_refid = "";
				} else if (drlen <= 4) {
					ZERO(u32);
					memcpy(&u32, value, drlen);
					dstadr_refid = refid_str(u32, 1);
				} else if (decodenetnum(value, &refidadr)) {
					if (SOCK_UNSPEC(&refidadr))
						dstadr_refid = "0.0.0.0";
					else if (ISREFCLOCKADR(&refidadr))
						dstadr_refid =
						    refnumtoa(&refidadr);
					else
						dstadr_refid =
						    stoa(&refidadr);
				} else {
					have_da_rid = FALSE;
				}
			} else if (   (pvl == apeervarlist)
				   || (pvl == peervarlist)) {
				/* no need to check drefid == REFID_HASH */
				have_da_rid = TRUE;
				drlen = strlen(value);
				if (0 == drlen) {
					dstadr_refid = "";
				} else if (drlen <= 4) {
					ZERO(u32);
					memcpy(&u32, value, drlen);
					dstadr_refid = refid_str(u32, 1);
					//fprintf(stderr, "apeervarlist S1 refid: value=<%s>\n", value);
				} else if (decodenetnum(value, &refidadr)) {
					if (SOCK_UNSPEC(&refidadr))
						dstadr_refid = "0.0.0.0";
					else if (ISREFCLOCKADR(&refidadr))
						dstadr_refid =
						    refnumtoa(&refidadr);
					else {
						char *buf = emalloc(10);
						int i = ntohl(refidadr.sa4.sin_addr.s_addr);

						snprintf(buf, 10,
							"%0x", i);
						dstadr_refid = buf;
					//fprintf(stderr, "apeervarlist refid: value=<%x>\n", i);
					}
					//fprintf(stderr, "apeervarlist refid: value=<%s>\n", value);
				} else {
					have_da_rid = FALSE;
				}
			}
		} else if (!strcmp("stratum", name)) {
			decodeuint(value, &stratum);
		} else if (!strcmp("hpoll", name)) {
			if (decodeint(value, &hpoll) && hpoll < 0)
				hpoll = NTP_MINPOLL;
		} else if (!strcmp("ppoll", name)) {
			if (decodeint(value, &ppoll) && ppoll < 0)
				ppoll = NTP_MINPOLL;
		} else if (!strcmp("reach", name)) {
			decodeuint(value, &reach);
		} else if (!strcmp("delay", name)) {
			decodetime(value, &estdelay);
		} else if (!strcmp("offset", name)) {
			decodetime(value, &estoffset);
		} else if (!strcmp("jitter", name)) {
			if ((pvl == peervarlist || pvl == apeervarlist)
			    && decodetime(value, &estjitter))
				have_jitter = 1;
		} else if (!strcmp("rootdisp", name) ||
			   !strcmp("dispersion", name)) {
			decodetime(value, &estdisp);
		} else if (!strcmp("rec", name)) {
			decodets(value, &rec);
		} else if (!strcmp("srcport", name) ||
			   !strcmp("peerport", name)) {
			decodeuint(value, &srcport);
		} else if (!strcmp("reftime", name)) {
			if (!decodets(value, &reftime))
				L_CLR(&reftime);
		} else if (!strcmp("flash", name)) {
		    decodeuint(value, &flash);
		} else {
			// fprintf(stderr, "UNRECOGNIZED name=%s ", name);
		}
	}

	/*
	 * hmode gives the best guidance for the t column.  If the response
	 * did not include hmode we'll use the old decodeaddrtype() result.
	 */
	switch (hmode) {

	case MODE_BCLIENT:
		/* broadcastclient or multicastclient */
		type = 'b';
		break;

	case MODE_BROADCAST:
		/* broadcast or multicast server */
		if (IS_MCAST(&srcadr))
			type = 'M';
		else
			type = 'B';
		break;

	case MODE_CLIENT:
		if (ISREFCLOCKADR(&srcadr))
			type = 'l';	/* local refclock*/
		else if (SOCK_UNSPEC(&srcadr))
			type = 'p';	/* pool */
		else if (IS_MCAST(&srcadr))
			type = 'a';	/* manycastclient */
		else
			type = 'u';	/* unicast */
		break;

	case MODE_ACTIVE:
		type = 's';		/* symmetric active */
		break;			/* configured */

	case MODE_PASSIVE:
		type = 'S';		/* symmetric passive */
		break;			/* ephemeral */
	}

	/*
	 * Got everything, format the line
	 */
	poll_sec = 1 << min(ppoll, hpoll);
	if (pktversion > NTP_OLDVERSION)
		c = flash3[CTL_PEER_STATVAL(rstatus) & 0x7];
	else
		c = flash2[CTL_PEER_STATVAL(rstatus) & 0x3];
	if (numhosts > 1) {
		if ((pvl == peervarlist || pvl == apeervarlist)
		    && have_dstadr) {
			serverlocal = nntohost_col(&dstadr,
			    (size_t)min(LIB_BUFLENGTH - 1, maxhostlen),
			    TRUE);
		} else {
			if (currenthostisnum)
				serverlocal = trunc_left(currenthost,
							 maxhostlen);
			else
				serverlocal = currenthost;
		}
		fprintf(fp, "%-*s ", (int)maxhostlen, serverlocal);
	}
	if (AF_UNSPEC == af || AF(&srcadr) == af) {
		if (!have_srchost)
			strlcpy(clock_name, nntohost(&srcadr),
				sizeof(clock_name));
		/* wide and long source - space over on next line */
		/* allow for host + sp if > 1 and regular tally + source + sp */
		if (wideremote && 15 < strlen(clock_name))
			fprintf(fp, "%c%s\n%*s", c, clock_name,
				((numhosts > 1) ? (int)maxhostlen + 1 : 0)
							+ 1 + 15 + 1, "");
		else
			fprintf(fp, "%c%-15.15s ", c, clock_name);
		if ((flash & TEST12) && (pvl != opeervarlist)) {
			drlen = fprintf(fp, "(loop)");
		} else if (!have_da_rid) {
			drlen = 0;
		} else {
			drlen = strlen(dstadr_refid);
			makeascii(drlen, dstadr_refid, fp);
		}
		if (pvl == apeervarlist) {
			while (drlen++ < 9)
				fputc(' ', fp);
			fprintf(fp, "%-6d", associd);
		} else {
			while (drlen++ < 15)
				fputc(' ', fp);
		}
		fprintf(fp,
			" %2ld %c %4.4s %4.4s  %3lo  %7.7s %8.7s %7.7s\n",
			stratum, type,
			prettyinterval(whenbuf, sizeof(whenbuf),
				       when(&ts, &rec, &reftime)),
			prettyinterval(pollbuf, sizeof(pollbuf), 
				       (int)poll_sec),
			reach, lfptoms(&estdelay, 3),
			lfptoms(&estoffset, 3),
			(have_jitter)
			    ? lfptoms(&estjitter, 3)
			    : lfptoms(&estdisp, 3));
		return (1);
	}
	else
		return(1);
}


/*
 * dogetpeers - given an association ID, read and print the spreadsheet
 *		peer variables.
 */
static int
dogetpeers(
	struct varlist *pvl,
	associd_t associd,
	FILE *fp,
	int af
	)
{
	const char *datap;
	int res;
	size_t dsize;
	u_short rstatus;

#ifdef notdef
	res = doquerylist(pvl, CTL_OP_READVAR, associd, 0, &rstatus,
			  &dsize, &datap);
#else
	/*
	 * Damn fuzzballs
	 */
	res = doquery(CTL_OP_READVAR, associd, 0, 0, NULL, &rstatus,
			  &dsize, &datap);
#endif

	if (res != 0)
		return 0;

	if (dsize == 0) {
		if (numhosts > 1)
			fprintf(stderr, "server=%s ", currenthost);
		fprintf(stderr,
			"***No information returned for association %u\n",
			associd);
		return 0;
	}

	return doprintpeers(pvl, associd, (int)rstatus, dsize, datap,
			    fp, af);
}


/*
 * peers - print a peer spreadsheet
 */
static void
dopeers(
	int showall,
	FILE *fp,
	int af
	)
{
	u_int		u;
	char		fullname[LENHOSTNAME];
	sockaddr_u	netnum;
	const char *	name_or_num;
	size_t		sl;

	if (!dogetassoc(fp))
		return;

	for (u = 0; u < numhosts; u++) {
		if (getnetnum(chosts[u].name, &netnum, fullname, af)) {
			name_or_num = nntohost(&netnum);
			sl = strlen(name_or_num);
			maxhostlen = max(maxhostlen, sl);
		}
	}
	if (numhosts > 1)
		fprintf(fp, "%-*.*s ", (int)maxhostlen, (int)maxhostlen,
			"server (local)");
	fprintf(fp,
		"     remote           refid      st t when poll reach   delay   offset  jitter\n");
	if (numhosts > 1)
		for (u = 0; u <= maxhostlen; u++)
			fprintf(fp, "=");
	fprintf(fp,
		"==============================================================================\n");

	for (u = 0; u < numassoc; u++) {
		if (!showall &&
		    !(CTL_PEER_STATVAL(assoc_cache[u].status)
		      & (CTL_PST_CONFIG|CTL_PST_REACH))) {
			if (debug)
				fprintf(stderr, "eliding [%d]\n",
					(int)assoc_cache[u].assid);
			continue;
		}
		if (!dogetpeers(peervarlist, (int)assoc_cache[u].assid,
				fp, af))
			return;
	}
	return;
}


/*
 * doapeers - print a peer spreadsheet with assocIDs
 */
static void
doapeers(
	int showall,
	FILE *fp,
	int af
	)
{
	u_int		u;
	char		fullname[LENHOSTNAME];
	sockaddr_u	netnum;
	const char *	name_or_num;
	size_t		sl;

	if (!dogetassoc(fp))
		return;

	for (u = 0; u < numhosts; u++) {
		if (getnetnum(chosts[u].name, &netnum, fullname, af)) {
			name_or_num = nntohost(&netnum);
			sl = strlen(name_or_num);
			maxhostlen = max(maxhostlen, sl);
		}
	}
	if (numhosts > 1)
		fprintf(fp, "%-*.*s ", (int)maxhostlen, (int)maxhostlen,
			"server (local)");
	fprintf(fp,
		"     remote       refid   assid  st t when poll reach   delay   offset  jitter\n");
	if (numhosts > 1)
		for (u = 0; u <= maxhostlen; u++)
			fprintf(fp, "=");
	fprintf(fp,
		"==============================================================================\n");

	for (u = 0; u < numassoc; u++) {
		if (!showall &&
		    !(CTL_PEER_STATVAL(assoc_cache[u].status)
		      & (CTL_PST_CONFIG|CTL_PST_REACH))) {
			if (debug)
				fprintf(stderr, "eliding [%d]\n",
					(int)assoc_cache[u].assid);
			continue;
		}
		if (!dogetpeers(apeervarlist, (int)assoc_cache[u].assid,
				fp, af))
			return;
	}
	return;
}


/*
 * peers - print a peer spreadsheet
 */
/*ARGSUSED*/
static void
peers(
	struct parse *pcmd,
	FILE *fp
	)
{
	if (drefid == REFID_HASH) {
		apeers(pcmd, fp);
	} else {
		int af = 0;

		if (pcmd->nargs == 1) {
			if (pcmd->argval->ival == 6)
				af = AF_INET6;
			else
				af = AF_INET;
		}
		dopeers(0, fp, af);
	}
}


/*
 * apeers - print a peer spreadsheet, with assocIDs
 */
/*ARGSUSED*/
static void
apeers(
	struct parse *pcmd,
	FILE *fp
	)
{
	int af = 0;

	if (pcmd->nargs == 1) {
		if (pcmd->argval->ival == 6)
			af = AF_INET6;
		else
			af = AF_INET;
	}
	doapeers(0, fp, af);
}


/*
 * lpeers - print a peer spreadsheet including all fuzzball peers
 */
/*ARGSUSED*/
static void
lpeers(
	struct parse *pcmd,
	FILE *fp
	)
{
	int af = 0;

	if (pcmd->nargs == 1) {
		if (pcmd->argval->ival == 6)
			af = AF_INET6;
		else
			af = AF_INET;
	}
	dopeers(1, fp, af);
}


/*
 * opeers - print a peer spreadsheet
 */
static void
doopeers(
	int showall,
	FILE *fp,
	int af
	)
{
	u_int i;
	char fullname[LENHOSTNAME];
	sockaddr_u netnum;

	if (!dogetassoc(fp))
		return;

	for (i = 0; i < numhosts; ++i) {
		if (getnetnum(chosts[i].name, &netnum, fullname, af))
			if (strlen(fullname) > maxhostlen)
				maxhostlen = strlen(fullname);
	}
	if (numhosts > 1)
		fprintf(fp, "%-*.*s ", (int)maxhostlen, (int)maxhostlen,
			"server");
	fprintf(fp,
	    "     remote           local      st t when poll reach   delay   offset    disp\n");
	if (numhosts > 1)
		for (i = 0; i <= maxhostlen; ++i)
			fprintf(fp, "=");
	fprintf(fp,
	    "==============================================================================\n");

	for (i = 0; i < numassoc; i++) {
		if (!showall &&
		    !(CTL_PEER_STATVAL(assoc_cache[i].status) &
		      (CTL_PST_CONFIG | CTL_PST_REACH)))
			continue;
		if (!dogetpeers(opeervarlist, assoc_cache[i].assid, fp, af))
			return;
	}
	return;
}


/*
 * opeers - print a peer spreadsheet the old way
 */
/*ARGSUSED*/
static void
opeers(
	struct parse *pcmd,
	FILE *fp
	)
{
	int af = 0;

	if (pcmd->nargs == 1) {
		if (pcmd->argval->ival == 6)
			af = AF_INET6;
		else
			af = AF_INET;
	}
	doopeers(0, fp, af);
}


/*
 * lopeers - print a peer spreadsheet including all fuzzball peers
 */
/*ARGSUSED*/
static void
lopeers(
	struct parse *pcmd,
	FILE *fp
	)
{
	int af = 0;

	if (pcmd->nargs == 1) {
		if (pcmd->argval->ival == 6)
			af = AF_INET6;
		else
			af = AF_INET;
	}
	doopeers(1, fp, af);
}


/* 
 * config - send a configuration command to a remote host
 */
static void 
config (
	struct parse *pcmd,
	FILE *fp
	)
{
	const char *cfgcmd;
	u_short rstatus;
	size_t rsize;
	const char *rdata;
	char *resp;
	int res;
	int col;
	int i;

	cfgcmd = pcmd->argval[0].string;

	if (debug > 2)
		fprintf(stderr, 
			"In Config\n"
			"Keyword = %s\n"
			"Command = %s\n", pcmd->keyword, cfgcmd);

	res = doquery(CTL_OP_CONFIGURE, 0, 1,
		      strlen(cfgcmd), cfgcmd,
		      &rstatus, &rsize, &rdata);

	if (res != 0)
		return;

	if (rsize > 0 && '\n' == rdata[rsize - 1])
		rsize--;

	resp = emalloc(rsize + 1);
	memcpy(resp, rdata, rsize);
	resp[rsize] = '\0';

	col = -1;
	if (1 == sscanf(resp, "column %d syntax error", &col)
	    && col >= 0 && (size_t)col <= strlen(cfgcmd) + 1) {
		if (interactive)
			fputs("             *", stdout); /* "ntpq> :config " */
		else
			printf("%s\n", cfgcmd);
		for (i = 0; i < col; i++)
			fputc('_', stdout);
		fputs("^\n", stdout);
	}
	printf("%s\n", resp);
	free(resp);
}


/* 
 * config_from_file - remotely configure an ntpd daemon using the
 * specified configuration file
 * SK: This function is a kludge at best and is full of bad design
 * bugs:
 * 1. ntpq uses UDP, which means that there is no guarantee of in-order,
 *    error-free delivery. 
 * 2. The maximum length of a packet is constrained, and as a result, the
 *    maximum length of a line in a configuration file is constrained. 
 *    Longer lines will lead to unpredictable results.
 * 3. Since this function is sending a line at a time, we can't update
 *    the control key through the configuration file (YUCK!!)
 *
 * Pearly: There are a few places where 'size_t' is cast to 'int' based
 * on the assumption that 'int' can hold the size of the involved
 * buffers without overflow.
 */
static void 
config_from_file (
	struct parse *pcmd,
	FILE *fp
	)
{
	u_short rstatus;
	size_t rsize;
	const char *rdata;
	char * cp;
	int res;
	FILE *config_fd;
	char config_cmd[MAXLINE];
	size_t config_len;
	int i;
	int retry_limit;

	if (debug > 2)
		fprintf(stderr,
			"In Config\n"
			"Keyword = %s\n"
			"Filename = %s\n", pcmd->keyword,
			pcmd->argval[0].string);

	config_fd = fopen(pcmd->argval[0].string, "r");
	if (NULL == config_fd) {
		printf("ERROR!! Couldn't open file: %s\n",
		       pcmd->argval[0].string);
		return;
	}

	printf("Sending configuration file, one line at a time.\n");
	i = 0;
	while (fgets(config_cmd, MAXLINE, config_fd) != NULL) {
		/* Eliminate comments first. */
		cp = strchr(config_cmd, '#');
		config_len = (NULL != cp)
		    ? (size_t)(cp - config_cmd)
		    : strlen(config_cmd);
		
		/* [Bug 3015] make sure there's no trailing whitespace;
		 * the fix for [Bug 2853] on the server side forbids
		 * those. And don't transmit empty lines, as this would
		 * just be waste.
		 */
		while (config_len != 0 &&
		       (u_char)config_cmd[config_len-1] <= ' ')
			--config_len;
		config_cmd[config_len] = '\0';

		++i;
		if (0 == config_len)
			continue;

		retry_limit = 2;
		do 
			res = doquery(CTL_OP_CONFIGURE, 0, 1,
				      config_len, config_cmd,
				      &rstatus, &rsize, &rdata);
		while (res != 0 && retry_limit--);
		if (res != 0) {
			printf("Line No: %d query failed: %.*s\n"
			       "Subsequent lines not sent.\n",
			       i, (int)config_len, config_cmd);
			fclose(config_fd);
			return;
		}

		/* Right-strip the result code string, then output the
		 * last line executed, with result code. */
		while (rsize != 0 && (u_char)rdata[rsize - 1] <= ' ')
			--rsize;
		printf("Line No: %d %.*s: %.*s\n", i,
		       (int)rsize, rdata,
		       (int)config_len, config_cmd);
	}
	printf("Done sending file\n");
	fclose(config_fd);
}


static int
fetch_nonce(
	char *	nonce,
	size_t	cb_nonce
	)
{
	const char	nonce_eq[] = "nonce=";
	int		qres;
	u_short		rstatus;
	size_t		rsize;
	const char *	rdata;
	size_t		chars;

	/*
	 * Retrieve a nonce specific to this client to demonstrate to
	 * ntpd that we're capable of receiving responses to our source
	 * IP address, and thereby unlikely to be forging the source.
	 */
	qres = doquery(CTL_OP_REQ_NONCE, 0, 0, 0, NULL, &rstatus,
		       &rsize, &rdata);
	if (qres) {
		fprintf(stderr, "nonce request failed\n");
		return FALSE;
	}

	if ((size_t)rsize <= sizeof(nonce_eq) - 1 ||
	    strncmp(rdata, nonce_eq, sizeof(nonce_eq) - 1)) {
		fprintf(stderr, "unexpected nonce response format: %.*s\n",
			(int)rsize, rdata); /* cast is wobbly */
		return FALSE;
	}
	chars = rsize - (sizeof(nonce_eq) - 1);
	if (chars >= cb_nonce)
		return FALSE;
	memcpy(nonce, rdata + sizeof(nonce_eq) - 1, chars);
	nonce[chars] = '\0';
	while (chars > 0 &&
	       ('\r' == nonce[chars - 1] || '\n' == nonce[chars - 1])) {
		chars--;
		nonce[chars] = '\0';
	}
	
	return TRUE;
}


/*
 * add_mru	Add and entry to mru list, hash table, and allocate
 *		and return a replacement.
 *		This is a helper for collect_mru_list().
 */
static mru *
add_mru(
	mru *add
	)
{
	u_short hash;
	mru *mon;
	mru *unlinked;


	hash = NTP_HASH_ADDR(&add->addr);
	/* see if we have it among previously received entries */
	for (mon = hash_table[hash]; mon != NULL; mon = mon->hlink)
		if (SOCK_EQ(&mon->addr, &add->addr))
			break;
	if (mon != NULL) {
		if (!L_ISGEQ(&add->first, &mon->first)) {
			fprintf(stderr,
				"add_mru duplicate %s new first ts %08x.%08x precedes prior %08x.%08x\n",
				sptoa(&add->addr), add->last.l_ui,
				add->last.l_uf, mon->last.l_ui,
				mon->last.l_uf);
			exit(1);
		}
		UNLINK_DLIST(mon, mlink);
		UNLINK_SLIST(unlinked, hash_table[hash], mon, hlink, mru);
		INSIST(unlinked == mon);
		mru_dupes++;
		TRACE(2, ("(updated from %08x.%08x) ", mon->last.l_ui,
		      mon->last.l_uf));
	}
	LINK_DLIST(mru_list, add, mlink);
	LINK_SLIST(hash_table[hash], add, hlink);
	TRACE(2, ("add_mru %08x.%08x c %d m %d v %d rest %x first %08x.%08x %s\n",
	      add->last.l_ui, add->last.l_uf, add->count,
	      (int)add->mode, (int)add->ver, (u_int)add->rs,
	      add->first.l_ui, add->first.l_uf, sptoa(&add->addr)));
	/* if we didn't update an existing entry, alloc replacement */
	if (NULL == mon) {
		mon = emalloc(sizeof(*mon));
		mru_count++;
	}
	ZERO(*mon);

	return mon;
}


/* MGOT macro is specific to collect_mru_list() */
#define MGOT(bit)				\
	do {					\
		got |= (bit);			\
		if (MRU_GOT_ALL == got) {	\
			got = 0;		\
			mon = add_mru(mon);	\
			ci++;			\
		}				\
	} while (0)


int
mrulist_ctrl_c_hook(void)
{
	mrulist_interrupted = TRUE;
	return TRUE;
}


static int
collect_mru_list(
	const char *	parms,
	l_fp *		pnow
	)
{
	const u_int sleep_msecs = 5;
	static int ntpd_row_limit = MRU_ROW_LIMIT;
	int c_mru_l_rc;		/* this function's return code */
	u_char got;		/* MRU_GOT_* bits */
	time_t next_report;
	size_t cb;
	mru *mon;
	mru *head;
	mru *recent;
	int list_complete;
	char nonce[128];
	char buf[128];
	char req_buf[CTL_MAX_DATA_LEN];
	char *req;
	char *req_end;
	size_t chars;
	int qres;
	u_short rstatus;
	size_t rsize;
	const char *rdata;
	int limit;
	int frags;
	int cap_frags;
	char *tag;
	char *val;
	int si;		/* server index in response */
	int ci;		/* client (our) index for validation */
	int ri;		/* request index (.# suffix) */
	int mv;
	l_fp newest;
	l_fp last_older;
	sockaddr_u addr_older;
	int have_now;
	int have_addr_older; 
	int have_last_older;
	u_int restarted_count;
	u_int nonce_uses;
	u_short hash;
	mru *unlinked;

	if (!fetch_nonce(nonce, sizeof(nonce)))
		return FALSE;

	nonce_uses = 0;
	restarted_count = 0;
	mru_count = 0;
	INIT_DLIST(mru_list, mlink);
	cb = NTP_HASH_SIZE * sizeof(*hash_table);
	INSIST(NULL == hash_table);
	hash_table = emalloc_zero(cb);

	c_mru_l_rc = FALSE;
	list_complete = FALSE;
	have_now = FALSE;
	cap_frags = TRUE;
	got = 0;
	ri = 0;
	cb = sizeof(*mon);
	mon = emalloc_zero(cb);
	ZERO(*pnow);
	ZERO(last_older);
	next_report = time(NULL) + MRU_REPORT_SECS;

	limit = min(3 * MAXFRAGS, ntpd_row_limit);
	frags = MAXFRAGS;
	snprintf(req_buf, sizeof(req_buf), "nonce=%s, frags=%d%s",
		 nonce, frags, parms);
	nonce_uses++;

	while (TRUE) {
		if (debug)
			fprintf(stderr, "READ_MRU parms: %s\n", req_buf);

		qres = doqueryex(CTL_OP_READ_MRU, 0, 0,
				 strlen(req_buf), req_buf,
				 &rstatus, &rsize, &rdata, TRUE);

		if (CERR_UNKNOWNVAR == qres && ri > 0) {
			/*
			 * None of the supplied prior entries match, so
			 * toss them from our list and try again.
			 */
			if (debug)
				fprintf(stderr,
					"no overlap between %d prior entries and server MRU list\n",
					ri);
			while (ri--) {
				recent = HEAD_DLIST(mru_list, mlink);
				INSIST(recent != NULL);
				if (debug)
					fprintf(stderr,
						"tossing prior entry %s to resync\n",
						sptoa(&recent->addr));
				UNLINK_DLIST(recent, mlink);
				hash = NTP_HASH_ADDR(&recent->addr);
				UNLINK_SLIST(unlinked, hash_table[hash],
					     recent, hlink, mru);
				INSIST(unlinked == recent);
				free(recent);
				mru_count--;
			}
			if (NULL == HEAD_DLIST(mru_list, mlink)) {
				restarted_count++;
				if (restarted_count > 8) {
					fprintf(stderr,
						"Giving up after 8 restarts from the beginning.\n"
						"With high-traffic NTP servers, this can occur if the\n"
						"MRU list is limited to less than about 16 seconds' of\n"
						"entries.  See the 'mru' ntp.conf directive to adjust.\n");
					goto cleanup_return;
				}
				if (debug)
					fprintf(stderr,
						"--->   Restarting from the beginning, retry #%u\n", 
						restarted_count);
			}
		} else if (CERR_UNKNOWNVAR == qres) {
			fprintf(stderr,
				"CERR_UNKNOWNVAR from ntpd but no priors given.\n");
			goto cleanup_return;
		} else if (CERR_BADVALUE == qres) {
			if (cap_frags) {
				cap_frags = FALSE;
				if (debug)
					fprintf(stderr,
						"Reverted to row limit from fragments limit.\n");
			} else {
				/* ntpd has lower cap on row limit */
				ntpd_row_limit--;
				limit = min(limit, ntpd_row_limit);
				if (debug)
					fprintf(stderr,
						"Row limit reduced to %d following CERR_BADVALUE.\n",
						limit);
			}
		} else if (ERR_INCOMPLETE == qres ||
			   ERR_TIMEOUT == qres) {
			/*
			 * Reduce the number of rows/frags requested by
			 * half to recover from lost response fragments.
			 */
			if (cap_frags) {
				frags = max(2, frags / 2);
				if (debug)
					fprintf(stderr,
						"Frag limit reduced to %d following incomplete response.\n",
						frags);
			} else {
				limit = max(2, limit / 2);
				if (debug)
					fprintf(stderr,
						"Row limit reduced to %d following incomplete response.\n",
						limit);
			}
		} else if (qres) {
			show_error_msg(qres, 0);
			goto cleanup_return;
		}
		/*
		 * This is a cheap cop-out implementation of rawmode
		 * output for mrulist.  A better approach would be to
		 * dump similar output after the list is collected by
		 * ntpq with a continuous sequence of indexes.  This
		 * cheap approach has indexes resetting to zero for
		 * each query/response, and duplicates are not 
		 * coalesced.
		 */
		if (!qres && rawmode)
			printvars(rsize, rdata, rstatus, TYPE_SYS, 1, stdout);
		ci = 0;
		have_addr_older = FALSE;
		have_last_older = FALSE;
		while (!qres && nextvar(&rsize, &rdata, &tag, &val)) {
			INSIST(tag && val);
			if (debug > 1)
				fprintf(stderr, "nextvar gave: %s = %s\n",
					tag, val);
			switch(tag[0]) {

			case 'a':
				if (!strcmp(tag, "addr.older")) {
					if (!have_last_older) {
						fprintf(stderr,
							"addr.older %s before last.older\n",
							val);
						goto cleanup_return;
					}
					if (!decodenetnum(val, &addr_older)) {
						fprintf(stderr,
							"addr.older %s garbled\n",
							val);
						goto cleanup_return;
					}
					hash = NTP_HASH_ADDR(&addr_older);
					for (recent = hash_table[hash];
					     recent != NULL;
					     recent = recent->hlink)
						if (ADDR_PORT_EQ(
						      &addr_older,
						      &recent->addr))
							break;
					if (NULL == recent) {
						fprintf(stderr,
							"addr.older %s not in hash table\n",
							val);
						goto cleanup_return;
					}
					if (!L_ISEQU(&last_older,
						     &recent->last)) {
						fprintf(stderr,
							"last.older %08x.%08x mismatches %08x.%08x expected.\n",
							last_older.l_ui,
							last_older.l_uf,
							recent->last.l_ui,
							recent->last.l_uf);
						goto cleanup_return;
					}
					have_addr_older = TRUE;
				} else if (1 != sscanf(tag, "addr.%d", &si)
					   || si != ci)
					goto nomatch;
				else if (decodenetnum(val, &mon->addr))
					MGOT(MRU_GOT_ADDR);
				break;

			case 'l':
				if (!strcmp(tag, "last.older")) {
					if ('0' != val[0] ||
					    'x' != val[1] ||
					    !hextolfp(val + 2, &last_older)) {
						fprintf(stderr,
							"last.older %s garbled\n",
							val);
						goto cleanup_return;
					}
					have_last_older = TRUE;
				} else if (!strcmp(tag, "last.newest")) {
					if (0 != got) {
						fprintf(stderr,
							"last.newest %s before complete row, got = 0x%x\n",
							val, (u_int)got);
						goto cleanup_return;
					}
					if (!have_now) {
						fprintf(stderr,
							"last.newest %s before now=\n",
							val);
						goto cleanup_return;
					}
					head = HEAD_DLIST(mru_list, mlink);
					if (NULL != head) {
						if ('0' != val[0] ||
						    'x' != val[1] ||
						    !hextolfp(val + 2, &newest) ||
						    !L_ISEQU(&newest,
							     &head->last)) {
							fprintf(stderr,
								"last.newest %s mismatches %08x.%08x",
								val,
								head->last.l_ui,
								head->last.l_uf);
							goto cleanup_return;
						}
					}
					list_complete = TRUE;
				} else if (1 != sscanf(tag, "last.%d", &si) ||
					   si != ci || '0' != val[0] ||
					   'x' != val[1] ||
					   !hextolfp(val + 2, &mon->last)) {
					goto nomatch;
				} else {
					MGOT(MRU_GOT_LAST);
					/*
					 * allow interrupted retrieval,
					 * using most recent retrieved
					 * entry's last seen timestamp
					 * as the end of operation.
					 */
					*pnow = mon->last;
				}
				break;

			case 'f':
				if (1 != sscanf(tag, "first.%d", &si) ||
				    si != ci || '0' != val[0] ||
				    'x' != val[1] ||
				    !hextolfp(val + 2, &mon->first))
					goto nomatch;
				MGOT(MRU_GOT_FIRST);
				break;

			case 'n':
				if (!strcmp(tag, "nonce")) {
					strlcpy(nonce, val, sizeof(nonce));
					nonce_uses = 0;
					break; /* case */
				} else if (strcmp(tag, "now") ||
					   '0' != val[0] ||
					   'x' != val[1] ||
					    !hextolfp(val + 2, pnow))
					goto nomatch;
				have_now = TRUE;
				break;

			case 'c':
				if (1 != sscanf(tag, "ct.%d", &si) ||
				    si != ci ||
				    1 != sscanf(val, "%d", &mon->count)
				    || mon->count < 1)
					goto nomatch;
				MGOT(MRU_GOT_COUNT);
				break;

			case 'm':
				if (1 != sscanf(tag, "mv.%d", &si) ||
				    si != ci ||
				    1 != sscanf(val, "%d", &mv))
					goto nomatch;
				mon->mode = PKT_MODE(mv);
				mon->ver = PKT_VERSION(mv);
				MGOT(MRU_GOT_MV);
				break;

			case 'r':
				if (1 != sscanf(tag, "rs.%d", &si) ||
				    si != ci ||
				    1 != sscanf(val, "0x%hx", &mon->rs))
					goto nomatch;
				MGOT(MRU_GOT_RS);
				break;

			default:
			nomatch:	
				/* empty stmt */ ;
				/* ignore unknown tags */
			}
		}
		if (have_now)
			list_complete = TRUE;
		if (list_complete) {
			INSIST(0 == ri || have_addr_older);
		}
		if (mrulist_interrupted) {
			printf("mrulist retrieval interrupted by operator.\n"
			       "Displaying partial client list.\n");
			fflush(stdout);
		}
		if (list_complete || mrulist_interrupted) {
			fprintf(stderr,
				"\rRetrieved %u unique MRU entries and %u updates.\n",
				mru_count, mru_dupes);
			fflush(stderr);
			break;
		}
		if (time(NULL) >= next_report) {
			next_report += MRU_REPORT_SECS;
			fprintf(stderr, "\r%u (%u updates) ", mru_count,
				mru_dupes);
			fflush(stderr);
		}

		/*
		 * Snooze for a bit between queries to let ntpd catch
		 * up with other duties.
		 */
#ifdef SYS_WINNT
		Sleep(sleep_msecs);
#elif !defined(HAVE_NANOSLEEP)
		sleep((sleep_msecs / 1000) + 1);
#else
		{
			struct timespec interv = { 0,
						   1000 * sleep_msecs };
			nanosleep(&interv, NULL);
		}
#endif
		/*
		 * If there were no errors, increase the number of rows
		 * to a maximum of 3 * MAXFRAGS (the most packets ntpq
		 * can handle in one response), on the assumption that
		 * no less than 3 rows fit in each packet, capped at 
		 * our best guess at the server's row limit.
		 */
		if (!qres) {
			if (cap_frags) {
				frags = min(MAXFRAGS, frags + 1);
			} else {
				limit = min3(3 * MAXFRAGS,
					     ntpd_row_limit,
					     max(limit + 1,
					         limit * 33 / 32));
			}
		}
		/*
		 * prepare next query with as many address and last-seen
		 * timestamps as will fit in a single packet.
		 */
		req = req_buf;
		req_end = req_buf + sizeof(req_buf);
#define REQ_ROOM	(req_end - req)
		snprintf(req, REQ_ROOM, "nonce=%s, %s=%d%s", nonce,
			 (cap_frags)
			     ? "frags"
			     : "limit",
			 (cap_frags)
			     ? frags
			     : limit,
			 parms);
		req += strlen(req);
		nonce_uses++;
		if (nonce_uses >= 4) {
			if (!fetch_nonce(nonce, sizeof(nonce)))
				goto cleanup_return;
			nonce_uses = 0;
		}


		for (ri = 0, recent = HEAD_DLIST(mru_list, mlink);
		     recent != NULL;
		     ri++, recent = NEXT_DLIST(mru_list, recent, mlink)) {

			snprintf(buf, sizeof(buf),
				 ", addr.%d=%s, last.%d=0x%08x.%08x",
				 ri, sptoa(&recent->addr), ri,
				 recent->last.l_ui, recent->last.l_uf);
			chars = strlen(buf);
			if ((size_t)REQ_ROOM <= chars)
				break;
			memcpy(req, buf, chars + 1);
			req += chars;
		}
	}

	c_mru_l_rc = TRUE;
	goto retain_hash_table;

cleanup_return:
	free(hash_table);
	hash_table = NULL;

retain_hash_table:
	if (mon != NULL)
		free(mon);

	return c_mru_l_rc;
}


/*
 * qcmp_mru_addr - sort MRU entries by remote address.
 *
 * All IPv4 addresses sort before any IPv6, addresses are sorted by
 * value within address family.
 */
static int
qcmp_mru_addr(
	const void *v1,
	const void *v2
	)
{
	const mru * const *	ppm1 = v1;
	const mru * const *	ppm2 = v2;
	const mru *		pm1;
	const mru *		pm2;
	u_short			af1;
	u_short			af2;
	size_t			cmplen;
	size_t			addr_off;

	pm1 = *ppm1;
	pm2 = *ppm2;

	af1 = AF(&pm1->addr);
	af2 = AF(&pm2->addr);

	if (af1 != af2)
		return (AF_INET == af1)
			   ? -1
			   : 1;

	cmplen = SIZEOF_INADDR(af1);
	addr_off = (AF_INET == af1)
		      ? offsetof(struct sockaddr_in, sin_addr)
		      : offsetof(struct sockaddr_in6, sin6_addr);

	return memcmp((const char *)&pm1->addr + addr_off,
		      (const char *)&pm2->addr + addr_off,
		      cmplen);
}


static int
qcmp_mru_r_addr(
	const void *v1,
	const void *v2
	)
{
	return -qcmp_mru_addr(v1, v2);
}


/*
 * qcmp_mru_count - sort MRU entries by times seen (hit count).
 */
static int
qcmp_mru_count(
	const void *v1,
	const void *v2
	)
{
	const mru * const *	ppm1 = v1;
	const mru * const *	ppm2 = v2;
	const mru *		pm1;
	const mru *		pm2;

	pm1 = *ppm1;
	pm2 = *ppm2;
	
	return (pm1->count < pm2->count)
		   ? -1
		   : ((pm1->count == pm2->count)
			  ? 0
			  : 1);
}


static int
qcmp_mru_r_count(
	const void *v1,
	const void *v2
	)
{
	return -qcmp_mru_count(v1, v2);
}


/*
 * qcmp_mru_avgint - sort MRU entries by average interval.
 */
static int
qcmp_mru_avgint(
	const void *v1,
	const void *v2
	)
{
	const mru * const *	ppm1 = v1;
	const mru * const *	ppm2 = v2;
	const mru *		pm1;
	const mru *		pm2;
	l_fp			interval;
	double			avg1;
	double			avg2;

	pm1 = *ppm1;
	pm2 = *ppm2;

	interval = pm1->last;
	L_SUB(&interval, &pm1->first);
	LFPTOD(&interval, avg1);
	avg1 /= pm1->count;

	interval = pm2->last;
	L_SUB(&interval, &pm2->first);
	LFPTOD(&interval, avg2);
	avg2 /= pm2->count;

	if (avg1 < avg2)
		return -1;
	else if (avg1 > avg2)
		return 1;

	/* secondary sort on lstint - rarely tested */
	if (L_ISEQU(&pm1->last, &pm2->last))
		return 0;
	else if (L_ISGEQ(&pm1->last, &pm2->last))
		return -1;
	else
		return 1;
}


static int
qcmp_mru_r_avgint(
	const void *v1,
	const void *v2
	)
{
	return -qcmp_mru_avgint(v1, v2);
}


/*
 * mrulist - ntpq's mrulist command to fetch an arbitrarily large Most
 *	     Recently Used (seen) remote address list from ntpd.
 *
 * Similar to ntpdc's monlist command, but not limited to a single
 * request/response, and thereby not limited to a few hundred remote
 * addresses.
 *
 * See ntpd/ntp_control.c read_mru_list() for comments on the way
 * CTL_OP_READ_MRU is designed to be used.
 *
 * mrulist intentionally differs from monlist in the way the avgint
 * column is calculated.  monlist includes the time after the last
 * packet from the client until the monlist query time in the average,
 * while mrulist excludes it.  That is, monlist's average interval grows
 * over time for remote addresses not heard from in some time, while it
 * remains unchanged in mrulist.  This also affects the avgint value for
 * entries representing a single packet, with identical first and last
 * timestamps.  mrulist shows 0 avgint, monlist shows a value identical
 * to lstint.
 */
static void 
mrulist(
	struct parse *	pcmd,
	FILE *		fp
	)
{
	const char mincount_eq[] =	"mincount=";
	const char resall_eq[] =	"resall=";
	const char resany_eq[] =	"resany=";
	const char maxlstint_eq[] =	"maxlstint=";
	const char laddr_eq[] =		"laddr=";
	const char sort_eq[] =		"sort=";
	mru_sort_order order;
	size_t n;
	char parms_buf[128];
	char buf[24];
	char *parms;
	const char *arg;
	size_t cb;
	mru **sorted;
	mru **ppentry;
	mru *recent;
	l_fp now;
	l_fp interval;
	double favgint;
	double flstint;
	int avgint;
	int lstint;
	size_t i;

	mrulist_interrupted = FALSE;
	push_ctrl_c_handler(&mrulist_ctrl_c_hook);
	fprintf(stderr,
		"Ctrl-C will stop MRU retrieval and display partial results.\n");
	fflush(stderr);

	order = MRUSORT_DEF;
	parms_buf[0] = '\0';
	parms = parms_buf;
	for (i = 0; i < pcmd->nargs; i++) {
		arg = pcmd->argval[i].string;
		if (arg != NULL) {
			cb = strlen(arg) + 1;
			if ((!strncmp(resall_eq, arg, sizeof(resall_eq)
			    - 1) || !strncmp(resany_eq, arg,
			    sizeof(resany_eq) - 1) || !strncmp(
			    mincount_eq, arg, sizeof(mincount_eq) - 1) 
			    || !strncmp(laddr_eq, arg, sizeof(laddr_eq)
			    - 1) || !strncmp(maxlstint_eq, arg,
			    sizeof(laddr_eq) - 1)) && parms + cb + 2 <=
			    parms_buf + sizeof(parms_buf)) {
				/* these are passed intact to ntpd */
				memcpy(parms, ", ", 2);
				parms += 2;
				memcpy(parms, arg, cb);
				parms += cb - 1;
			} else if (!strncmp(sort_eq, arg,
					    sizeof(sort_eq) - 1)) {
				arg += sizeof(sort_eq) - 1;
				for (n = 0;
				     n < COUNTOF(mru_sort_keywords);
				     n++)
					if (!strcmp(mru_sort_keywords[n],
						    arg))
						break;
				if (n < COUNTOF(mru_sort_keywords))
					order = n;
			} else if (!strcmp("limited", arg) ||
				   !strcmp("kod", arg)) {
				/* transform to resany=... */
				snprintf(buf, sizeof(buf),
					 ", resany=0x%x",
					 ('k' == arg[0])
					     ? RES_KOD
					     : RES_LIMITED);
				cb = 1 + strlen(buf);
				if (parms + cb <
					parms_buf + sizeof(parms_buf)) {
					memcpy(parms, buf, cb);
					parms += cb - 1;
				}
			} else
				fprintf(stderr,
					"ignoring unrecognized mrulist parameter: %s\n",
					arg);
		}
	}
	parms = parms_buf;

	if (!collect_mru_list(parms, &now))
		return;

	/* display the results */
	if (rawmode)
		goto cleanup_return;

	/* construct an array of entry pointers in default order */
	sorted = eallocarray(mru_count, sizeof(*sorted));
	ppentry = sorted;
	if (MRUSORT_R_DEF != order) {
		ITER_DLIST_BEGIN(mru_list, recent, mlink, mru)
			INSIST(ppentry < sorted + mru_count);
			*ppentry = recent;
			ppentry++;
		ITER_DLIST_END()
	} else {
		REV_ITER_DLIST_BEGIN(mru_list, recent, mlink, mru)
			INSIST(ppentry < sorted + mru_count);
			*ppentry = recent;
			ppentry++;
		REV_ITER_DLIST_END()
	}

	if (ppentry - sorted != (int)mru_count) {
		fprintf(stderr,
			"mru_count %u should match MRU list depth %ld.\n",
			mru_count, (long)(ppentry - sorted));
		free(sorted);
		goto cleanup_return;
	}

	/* re-sort sorted[] if not default or reverse default */
	if (MRUSORT_R_DEF < order)
		qsort(sorted, mru_count, sizeof(sorted[0]),
		      mru_qcmp_table[order]);

	mrulist_interrupted = FALSE;
	printf(	"lstint avgint rstr r m v  count rport remote address\n"
		"==============================================================================\n");
		/* '=' x 78 */
	for (ppentry = sorted; ppentry < sorted + mru_count; ppentry++) {
		recent = *ppentry;
		interval = now;
		L_SUB(&interval, &recent->last);
		LFPTOD(&interval, flstint);
		lstint = (int)(flstint + 0.5);
		interval = recent->last;
		L_SUB(&interval, &recent->first);
		LFPTOD(&interval, favgint);
		favgint /= recent->count;
		avgint = (int)(favgint + 0.5);
		fprintf(fp, "%6d %6d %4hx %c %d %d %6d %5u %s\n",
			lstint, avgint, recent->rs,
			(RES_KOD & recent->rs)
			    ? 'K'
			    : (RES_LIMITED & recent->rs)
				  ? 'L'
				  : '.',
			(int)recent->mode, (int)recent->ver,
			recent->count, SRCPORT(&recent->addr),
			nntohost(&recent->addr));
		if (showhostnames)
			fflush(fp);
		if (mrulist_interrupted) {
			fputs("\n --interrupted--\n", fp);
			fflush(fp);
			break;
		}
	}
	fflush(fp);
	if (debug) {
		fprintf(stderr,
			"--- completed, freeing sorted[] pointers\n");
		fflush(stderr);
	}
	free(sorted);

cleanup_return:
	if (debug) {
		fprintf(stderr, "... freeing MRU entries\n");
		fflush(stderr);
	}
	ITER_DLIST_BEGIN(mru_list, recent, mlink, mru)
		free(recent);
	ITER_DLIST_END()
	if (debug) {
		fprintf(stderr, "... freeing hash_table[]\n");
		fflush(stderr);
	}
	free(hash_table);
	hash_table = NULL;
	INIT_DLIST(mru_list, mlink);

	pop_ctrl_c_handler(&mrulist_ctrl_c_hook);
}


/*
 * validate_ifnum - helper for ifstats()
 *
 * Ensures rows are received in order and complete.
 */
static void
validate_ifnum(
	FILE *		fp,
	u_int		ifnum,
	int *		pfields,
	ifstats_row *	prow
	)
{
	if (prow->ifnum == ifnum)
		return;
	if (prow->ifnum + 1 <= ifnum) {
		if (*pfields < IFSTATS_FIELDS)
			fprintf(fp, "Warning: incomplete row with %d (of %d) fields\n",
				*pfields, IFSTATS_FIELDS);
		*pfields = 0;
		prow->ifnum = ifnum;
		return;
	}
	fprintf(stderr,
		"received if index %u, have %d of %d fields for index %u, aborting.\n",
		ifnum, *pfields, IFSTATS_FIELDS, prow->ifnum);
	exit(1);
}


/*
 * another_ifstats_field - helper for ifstats()
 *
 * If all fields for the row have been received, print it.
 */
static void
another_ifstats_field(
	int *		pfields,
	ifstats_row *	prow,
	FILE *		fp
	)
{
	u_int ifnum;

	(*pfields)++;
	/* we understand 12 tags */
	if (IFSTATS_FIELDS > *pfields)	
		return;
	/*
	"    interface name                                        send\n"
	" #  address/broadcast     drop flag ttl mc received sent failed peers   uptime\n"
	"==============================================================================\n");
	 */
	fprintf(fp,
		"%3u %-24.24s %c %4x %3u %2u %6u %6u %6u %5u %8d\n"
		"    %s\n",
		prow->ifnum, prow->name,
		(prow->enabled)
		    ? '.'
		    : 'D',
		prow->flags, prow->ttl, prow->mcast_count,
		prow->received, prow->sent, prow->send_errors,
		prow->peer_count, prow->uptime, sptoa(&prow->addr));
	if (!SOCK_UNSPEC(&prow->bcast))
		fprintf(fp, "    %s\n", sptoa(&prow->bcast));
	ifnum = prow->ifnum;
	ZERO(*prow);
	prow->ifnum = ifnum;
}


/*
 * ifstats - ntpq -c ifstats modeled on ntpdc -c ifstats.
 */
static void 
ifstats(
	struct parse *	pcmd,
	FILE *		fp
	)
{
	const char	addr_fmt[] =	"addr.%u";
	const char	bcast_fmt[] =	"bcast.%u";
	const char	en_fmt[] =	"en.%u";	/* enabled */
	const char	flags_fmt[] =	"flags.%u";
	const char	mc_fmt[] =	"mc.%u";	/* mcast count */
	const char	name_fmt[] =	"name.%u";
	const char	pc_fmt[] =	"pc.%u";	/* peer count */
	const char	rx_fmt[] =	"rx.%u";
	const char	tl_fmt[] =	"tl.%u";	/* ttl */
	const char	tx_fmt[] =	"tx.%u";
	const char	txerr_fmt[] =	"txerr.%u";
	const char	up_fmt[] =	"up.%u";	/* uptime */
	const char *	datap;
	int		qres;
	size_t		dsize;
	u_short		rstatus;
	char *		tag;
	char *		val;
	int		fields;
	u_int		ui;
	ifstats_row	row;
	int		comprende;
	size_t		len;

	qres = doquery(CTL_OP_READ_ORDLIST_A, 0, TRUE, 0, NULL, &rstatus,
		       &dsize, &datap);
	if (qres)	/* message already displayed */
		return;

	fprintf(fp,
		"    interface name                                        send\n"
		" #  address/broadcast     drop flag ttl mc received sent failed peers   uptime\n"
		"==============================================================================\n");
		/* '=' x 78 */

	ZERO(row);
	fields = 0;
	ui = 0;
	while (nextvar(&dsize, &datap, &tag, &val)) {
		INSIST(tag && val);
		if (debug > 1)
		    fprintf(stderr, "nextvar gave: %s = %s\n", tag, val);
		comprende = FALSE;
		switch(tag[0]) {

		case 'a':
			if (1 == sscanf(tag, addr_fmt, &ui) &&
			    decodenetnum(val, &row.addr))
				comprende = TRUE;
			break;

		case 'b':
			if (1 == sscanf(tag, bcast_fmt, &ui) &&
			    ('\0' == *val ||
			     decodenetnum(val, &row.bcast)))
				comprende = TRUE;
			break;

		case 'e':
			if (1 == sscanf(tag, en_fmt, &ui) &&
			    1 == sscanf(val, "%d", &row.enabled))
				comprende = TRUE;
			break;

		case 'f':
			if (1 == sscanf(tag, flags_fmt, &ui) &&
			    1 == sscanf(val, "0x%x", &row.flags))
				comprende = TRUE;
			break;

		case 'm':
			if (1 == sscanf(tag, mc_fmt, &ui) &&
			    1 == sscanf(val, "%u", &row.mcast_count))
				comprende = TRUE;
			break;

		case 'n':
			if (1 == sscanf(tag, name_fmt, &ui)) {
				/* strip quotes */
				len = strlen(val);
				if (len >= 2 &&
				    len - 2 < sizeof(row.name)) {
					len -= 2;
					memcpy(row.name, val + 1, len);
					row.name[len] = '\0';
					comprende = TRUE;
				}
			}
			break;

		case 'p':
			if (1 == sscanf(tag, pc_fmt, &ui) &&
			    1 == sscanf(val, "%u", &row.peer_count))
				comprende = TRUE;
			break;

		case 'r':
			if (1 == sscanf(tag, rx_fmt, &ui) &&
			    1 == sscanf(val, "%u", &row.received))
				comprende = TRUE;
			break;

		case 't':
			if (1 == sscanf(tag, tl_fmt, &ui) &&
			    1 == sscanf(val, "%u", &row.ttl))
				comprende = TRUE;
			else if (1 == sscanf(tag, tx_fmt, &ui) &&
				 1 == sscanf(val, "%u", &row.sent))
				comprende = TRUE;
			else if (1 == sscanf(tag, txerr_fmt, &ui) &&
				 1 == sscanf(val, "%u", &row.send_errors))
				comprende = TRUE;
			break;

		case 'u':
			if (1 == sscanf(tag, up_fmt, &ui) &&
			    1 == sscanf(val, "%u", &row.uptime))
				comprende = TRUE;
			break;
		}

		if (comprende) {
			/* error out if rows out of order */
			validate_ifnum(fp, ui, &fields, &row);
			/* if the row is complete, print it */
			another_ifstats_field(&fields, &row, fp);
		}
	}
	if (fields != IFSTATS_FIELDS)
		fprintf(fp, "Warning: incomplete row with %d (of %d) fields\n",
			fields, IFSTATS_FIELDS);

	fflush(fp);
}


/*
 * validate_reslist_idx - helper for reslist()
 *
 * Ensures rows are received in order and complete.
 */
static void
validate_reslist_idx(
	FILE *		fp,
	u_int		idx,
	int *		pfields,
	reslist_row *	prow
	)
{
	if (prow->idx == idx)
		return;
	if (prow->idx + 1 == idx) {
		if (*pfields < RESLIST_FIELDS)
			fprintf(fp, "Warning: incomplete row with %d (of %d) fields",
				*pfields, RESLIST_FIELDS);
		*pfields = 0;
		prow->idx = idx;
		return;
	}
	fprintf(stderr,
		"received reslist index %u, have %d of %d fields for index %u, aborting.\n",
		idx, *pfields, RESLIST_FIELDS, prow->idx);
	exit(1);
}


/*
 * another_reslist_field - helper for reslist()
 *
 * If all fields for the row have been received, print it.
 */
static void
another_reslist_field(
	int *		pfields,
	reslist_row *	prow,
	FILE *		fp
	)
{
	char	addrmaskstr[128];
	int	prefix;	/* subnet mask as prefix bits count */
	u_int	idx;

	(*pfields)++;
	/* we understand 4 tags */
	if (RESLIST_FIELDS > *pfields)
		return;

	prefix = sockaddr_masktoprefixlen(&prow->mask);
	if (prefix >= 0)
		snprintf(addrmaskstr, sizeof(addrmaskstr), "%s/%d",
			 stoa(&prow->addr), prefix);
	else
		snprintf(addrmaskstr, sizeof(addrmaskstr), "%s %s",
			 stoa(&prow->addr), stoa(&prow->mask));

	/*
	"   hits    addr/prefix or addr mask\n"
	"           restrictions\n"
	"==============================================================================\n");
	 */
	fprintf(fp,
		"%10lu %s\n"
		"           %s\n",
		prow->hits, addrmaskstr, prow->flagstr);
	idx = prow->idx;
	ZERO(*prow);
	prow->idx = idx;
}


/*
 * reslist - ntpq -c reslist modeled on ntpdc -c reslist.
 */
static void 
reslist(
	struct parse *	pcmd,
	FILE *		fp
	)
{
	const char addr_fmtu[] =	"addr.%u";
	const char mask_fmtu[] =	"mask.%u";
	const char hits_fmt[] =		"hits.%u";
	const char flags_fmt[] =	"flags.%u";
	const char qdata[] =		"addr_restrictions";
	const int qdata_chars =		COUNTOF(qdata) - 1;
	const char *	datap;
	int		qres;
	size_t		dsize;
	u_short		rstatus;
	char *		tag;
	char *		val;
	int		fields;
	u_int		ui;
	reslist_row	row;
	int		comprende;
	size_t		len;

	qres = doquery(CTL_OP_READ_ORDLIST_A, 0, TRUE, qdata_chars,
		       qdata, &rstatus, &dsize, &datap);
	if (qres)	/* message already displayed */
		return;

	fprintf(fp,
		"   hits    addr/prefix or addr mask\n"
		"           restrictions\n"
		"==============================================================================\n");
		/* '=' x 78 */

	ZERO(row);
	fields = 0;
	ui = 0;
	while (nextvar(&dsize, &datap, &tag, &val)) {
		INSIST(tag && val);
		if (debug > 1)
			fprintf(stderr, "nextvar gave: %s = %s\n", tag, val);
		comprende = FALSE;
		switch(tag[0]) {

		case 'a':
			if (1 == sscanf(tag, addr_fmtu, &ui) &&
			    decodenetnum(val, &row.addr))
				comprende = TRUE;
			break;

		case 'f':
			if (1 == sscanf(tag, flags_fmt, &ui)) {
				if (NULL == val) {
					row.flagstr[0] = '\0';
					comprende = TRUE;
				} else if ((len = strlen(val)) < sizeof(row.flagstr)) {
					memcpy(row.flagstr, val, len);
					row.flagstr[len] = '\0';
					comprende = TRUE;
				} else {
					 /* no flags, and still !comprende */
					row.flagstr[0] = '\0';
				}
			}
			break;

		case 'h':
			if (1 == sscanf(tag, hits_fmt, &ui) &&
			    1 == sscanf(val, "%lu", &row.hits))
				comprende = TRUE;
			break;

		case 'm':
			if (1 == sscanf(tag, mask_fmtu, &ui) &&
			    decodenetnum(val, &row.mask))
				comprende = TRUE;
			break;
		}

		if (comprende) {
			/* error out if rows out of order */
			validate_reslist_idx(fp, ui, &fields, &row);
			/* if the row is complete, print it */
			another_reslist_field(&fields, &row, fp);
		}
	}
	if (fields != RESLIST_FIELDS)
		fprintf(fp, "Warning: incomplete row with %d (of %d) fields",
			fields, RESLIST_FIELDS);

	fflush(fp);
}


/*
 * collect_display_vdc
 */
static void 
collect_display_vdc(
	associd_t	as,
	vdc *		table,
	int		decodestatus,
	FILE *		fp
	)
{
	static const char * const suf[2] = { "adr", "port" };
	static const char * const leapbits[4] = { "00", "01",
						  "10", "11" };
	struct varlist vl[MAXLIST];
	char tagbuf[32];
	vdc *pvdc;
	u_short rstatus;
	size_t rsize;
	const char *rdata;
	int qres;
	char *tag;
	char *val;
	u_int n;
	size_t len;
	int match;
	u_long ul;
	int vtype;

	ZERO(vl);
	for (pvdc = table; pvdc->tag != NULL; pvdc++) {
		ZERO(pvdc->v);
		if (NTP_ADD != pvdc->type) {
			doaddvlist(vl, pvdc->tag);
		} else {
			for (n = 0; n < COUNTOF(suf); n++) {
				snprintf(tagbuf, sizeof(tagbuf), "%s%s",
					 pvdc->tag, suf[n]);
				doaddvlist(vl, tagbuf);
			}
		}
	}
	qres = doquerylist(vl, CTL_OP_READVAR, as, 0, &rstatus, &rsize,
			   &rdata);
	doclearvlist(vl);
	if (qres)
		return;		/* error msg already displayed */

	/*
	 * iterate over the response variables filling vdc_table with
	 * the retrieved values.
	 */
	while (nextvar(&rsize, &rdata, &tag, &val)) {
		INSIST(tag && val);
		n = 0;
		for (pvdc = table; pvdc->tag != NULL; pvdc++) {
			len = strlen(pvdc->tag);
			if (strncmp(tag, pvdc->tag, len))
				continue;
			if (NTP_ADD != pvdc->type) {
				if ('\0' != tag[len])
					continue;
				break;
			}
			match = FALSE;
			for (n = 0; n < COUNTOF(suf); n++) {
				if (strcmp(tag + len, suf[n]))
					continue;
				match = TRUE;
				break;
			}
			if (match)
				break;
		}
		if (NULL == pvdc->tag)
			continue;
		switch (pvdc->type) {

		case NTP_STR:
			/* strip surrounding double quotes */
			if ('"' == val[0]) {
				len = strlen(val);
				if (len > 0 && '"' == val[len - 1]) {
					val[len - 1] = '\0';
					val++;
				}
			}
			/* fallthru */
		case NTP_MODE:	/* fallthru */
		case NTP_2BIT:
			pvdc->v.str = estrdup(val);
			break;

		case NTP_LFP:
			decodets(val, &pvdc->v.lfp);
			break;

		case NTP_ADP:
			if (!decodenetnum(val, &pvdc->v.sau))
				fprintf(stderr, "malformed %s=%s\n",
					pvdc->tag, val);
			break;

		case NTP_ADD:
			if (0 == n) {	/* adr */
				if (!decodenetnum(val, &pvdc->v.sau))
					fprintf(stderr,
						"malformed %s=%s\n",
						pvdc->tag, val);
			} else {	/* port */
				if (atouint(val, &ul))
					SET_PORT(&pvdc->v.sau, 
						 (u_short)ul);
			}
			break;
		}
	}

	/* and display */
	if (decodestatus) {
		vtype = (0 == as)
			    ? TYPE_SYS
			    : TYPE_PEER;
		fprintf(fp, "associd=%u status=%04x %s,\n", as, rstatus,
			statustoa(vtype, rstatus));
	}

	for (pvdc = table; pvdc->tag != NULL; pvdc++) {
		switch (pvdc->type) {

		case NTP_STR:
			if (pvdc->v.str != NULL) {
				fprintf(fp, "%s  %s\n", pvdc->display,
					pvdc->v.str);
				free(pvdc->v.str);
				pvdc->v.str = NULL;
			}
			break;

		case NTP_ADD:	/* fallthru */
		case NTP_ADP:
			fprintf(fp, "%s  %s\n", pvdc->display,
				nntohostp(&pvdc->v.sau));
			break;

		case NTP_LFP:
			fprintf(fp, "%s  %s\n", pvdc->display,
				prettydate(&pvdc->v.lfp));
			break;

		case NTP_MODE:
			atouint(pvdc->v.str, &ul);
			fprintf(fp, "%s  %s\n", pvdc->display,
				modetoa((int)ul));
			break;

		case NTP_2BIT:
			atouint(pvdc->v.str, &ul);
			fprintf(fp, "%s  %s\n", pvdc->display,
				leapbits[ul & 0x3]);
			break;

		default:
			fprintf(stderr, "unexpected vdc type %d for %s\n",
				pvdc->type, pvdc->tag);
			break;
		}
	}
}


/*
 * sysstats - implements ntpq -c sysstats modeled on ntpdc -c sysstats
 */
static void
sysstats(
	struct parse *pcmd,
	FILE *fp
	)
{
    static vdc sysstats_vdc[] = {
	VDC_INIT("ss_uptime",		"uptime:               ", NTP_STR),
	VDC_INIT("ss_reset",		"sysstats reset:       ", NTP_STR),
	VDC_INIT("ss_received",		"packets received:     ", NTP_STR),
	VDC_INIT("ss_thisver",		"current version:      ", NTP_STR),
	VDC_INIT("ss_oldver",		"older version:        ", NTP_STR),
	VDC_INIT("ss_badformat",	"bad length or format: ", NTP_STR),
	VDC_INIT("ss_badauth",		"authentication failed:", NTP_STR),
	VDC_INIT("ss_declined",		"declined:             ", NTP_STR),
	VDC_INIT("ss_restricted",	"restricted:           ", NTP_STR),
	VDC_INIT("ss_limited",		"rate limited:         ", NTP_STR),
	VDC_INIT("ss_kodsent",		"KoD responses:        ", NTP_STR),
	VDC_INIT("ss_processed",	"processed for time:   ", NTP_STR),
#if 0
	VDC_INIT("ss_lamport",		"Lamport violations:    ", NTP_STR),
	VDC_INIT("ss_tsrounding",	"bad timestamp rounding:", NTP_STR),
#endif
	VDC_INIT(NULL,			NULL,			  0)
    };

	collect_display_vdc(0, sysstats_vdc, FALSE, fp);
}


/*
 * sysinfo - modeled on ntpdc's sysinfo
 */
static void
sysinfo(
	struct parse *pcmd,
	FILE *fp
	)
{
    static vdc sysinfo_vdc[] = {
	VDC_INIT("peeradr",		"system peer:      ", NTP_ADP),
	VDC_INIT("peermode",		"system peer mode: ", NTP_MODE),
	VDC_INIT("leap",		"leap indicator:   ", NTP_2BIT),
	VDC_INIT("stratum",		"stratum:          ", NTP_STR),
	VDC_INIT("precision",		"log2 precision:   ", NTP_STR),
	VDC_INIT("rootdelay",		"root delay:       ", NTP_STR),
	VDC_INIT("rootdisp",		"root dispersion:  ", NTP_STR),
	VDC_INIT("refid",		"reference ID:     ", NTP_STR),
	VDC_INIT("reftime",		"reference time:   ", NTP_LFP),
	VDC_INIT("sys_jitter",		"system jitter:    ", NTP_STR),
	VDC_INIT("clk_jitter",		"clock jitter:     ", NTP_STR),
	VDC_INIT("clk_wander",		"clock wander:     ", NTP_STR),
	VDC_INIT("bcastdelay",		"broadcast delay:  ", NTP_STR),
	VDC_INIT("authdelay",		"symm. auth. delay:", NTP_STR),
	VDC_INIT(NULL,			NULL,		      0)
    };

	collect_display_vdc(0, sysinfo_vdc, TRUE, fp);
}


/*
 * kerninfo - modeled on ntpdc's kerninfo
 */
static void
kerninfo(
	struct parse *pcmd,
	FILE *fp
	)
{
    static vdc kerninfo_vdc[] = {
	VDC_INIT("koffset",		"pll offset:          ", NTP_STR),
	VDC_INIT("kfreq",		"pll frequency:       ", NTP_STR),
	VDC_INIT("kmaxerr",		"maximum error:       ", NTP_STR),
	VDC_INIT("kesterr",		"estimated error:     ", NTP_STR),
	VDC_INIT("kstflags",		"kernel status:       ", NTP_STR),
	VDC_INIT("ktimeconst",		"pll time constant:   ", NTP_STR),
	VDC_INIT("kprecis",		"precision:           ", NTP_STR),
	VDC_INIT("kfreqtol",		"frequency tolerance: ", NTP_STR),
	VDC_INIT("kppsfreq",		"pps frequency:       ", NTP_STR),
	VDC_INIT("kppsstab",		"pps stability:       ", NTP_STR),
	VDC_INIT("kppsjitter",		"pps jitter:          ", NTP_STR),
	VDC_INIT("kppscalibdur",	"calibration interval ", NTP_STR),
	VDC_INIT("kppscalibs",		"calibration cycles:  ", NTP_STR),
	VDC_INIT("kppsjitexc",		"jitter exceeded:     ", NTP_STR),
	VDC_INIT("kppsstbexc",		"stability exceeded:  ", NTP_STR),
	VDC_INIT("kppscaliberrs",	"calibration errors:  ", NTP_STR),
	VDC_INIT(NULL,			NULL,			 0)
    };

	collect_display_vdc(0, kerninfo_vdc, TRUE, fp);
}


/*
 * monstats - implements ntpq -c monstats
 */
static void
monstats(
	struct parse *pcmd,
	FILE *fp
	)
{
    static vdc monstats_vdc[] = {
	VDC_INIT("mru_enabled",		"enabled:            ", NTP_STR),
	VDC_INIT("mru_depth",		"addresses:          ", NTP_STR),
	VDC_INIT("mru_deepest",		"peak addresses:     ", NTP_STR),
	VDC_INIT("mru_maxdepth",	"maximum addresses:  ", NTP_STR),
	VDC_INIT("mru_mindepth",	"reclaim above count:", NTP_STR),
	VDC_INIT("mru_maxage",		"reclaim older than: ", NTP_STR),
	VDC_INIT("mru_mem",		"kilobytes:          ", NTP_STR),
	VDC_INIT("mru_maxmem",		"maximum kilobytes:  ", NTP_STR),
	VDC_INIT(NULL,			NULL,			0)
    };

	collect_display_vdc(0, monstats_vdc, FALSE, fp);
}


/*
 * iostats - ntpq -c iostats - network input and output counters
 */
static void
iostats(
	struct parse *pcmd,
	FILE *fp
	)
{
    static vdc iostats_vdc[] = {
	VDC_INIT("iostats_reset",	"time since reset:     ", NTP_STR),
	VDC_INIT("total_rbuf",		"receive buffers:      ", NTP_STR),
	VDC_INIT("free_rbuf",		"free receive buffers: ", NTP_STR),
	VDC_INIT("used_rbuf",		"used receive buffers: ", NTP_STR),
	VDC_INIT("rbuf_lowater",	"low water refills:    ", NTP_STR),
	VDC_INIT("io_dropped",		"dropped packets:      ", NTP_STR),
	VDC_INIT("io_ignored",		"ignored packets:      ", NTP_STR),
	VDC_INIT("io_received",		"received packets:     ", NTP_STR),
	VDC_INIT("io_sent",		"packets sent:         ", NTP_STR),
	VDC_INIT("io_sendfailed",	"packet send failures: ", NTP_STR),
	VDC_INIT("io_wakeups",		"input wakeups:        ", NTP_STR),
	VDC_INIT("io_goodwakeups",	"useful input wakeups: ", NTP_STR),
	VDC_INIT(NULL,			NULL,			  0)
    };

	collect_display_vdc(0, iostats_vdc, FALSE, fp);
}


/*
 * timerstats - ntpq -c timerstats - interval timer counters
 */
static void
timerstats(
	struct parse *pcmd,
	FILE *fp
	)
{
    static vdc timerstats_vdc[] = {
	VDC_INIT("timerstats_reset",	"time since reset:  ", NTP_STR),
	VDC_INIT("timer_overruns",	"timer overruns:    ", NTP_STR),
	VDC_INIT("timer_xmts",		"calls to transmit: ", NTP_STR),
	VDC_INIT(NULL,			NULL,		       0)
    };

	collect_display_vdc(0, timerstats_vdc, FALSE, fp);
}


/*
 * authinfo - implements ntpq -c authinfo
 */
static void
authinfo(
	struct parse *pcmd,
	FILE *fp
	)
{
    static vdc authinfo_vdc[] = {
	VDC_INIT("authreset",		"time since reset:", NTP_STR),
	VDC_INIT("authkeys",		"stored keys:     ", NTP_STR),
	VDC_INIT("authfreek",		"free keys:       ", NTP_STR),
	VDC_INIT("authklookups",	"key lookups:     ", NTP_STR),
	VDC_INIT("authknotfound",	"keys not found:  ", NTP_STR),
	VDC_INIT("authkuncached",	"uncached keys:   ", NTP_STR),
	VDC_INIT("authkexpired",	"expired keys:    ", NTP_STR),
	VDC_INIT("authencrypts",	"encryptions:     ", NTP_STR),
	VDC_INIT("authdecrypts",	"decryptions:     ", NTP_STR),
	VDC_INIT(NULL,			NULL,		     0)
    };

	collect_display_vdc(0, authinfo_vdc, FALSE, fp);
}


/*
 * pstats - show statistics for a peer
 */
static void
pstats(
	struct parse *pcmd,
	FILE *fp
	)
{
    static vdc pstats_vdc[] = {
	VDC_INIT("src",		"remote host:         ", NTP_ADD),
	VDC_INIT("dst",		"local address:       ", NTP_ADD),
	VDC_INIT("timerec",	"time last received:  ", NTP_STR),
	VDC_INIT("timer",	"time until next send:", NTP_STR),
	VDC_INIT("timereach",	"reachability change: ", NTP_STR),
	VDC_INIT("sent",	"packets sent:        ", NTP_STR),
	VDC_INIT("received",	"packets received:    ", NTP_STR),
	VDC_INIT("badauth",	"bad authentication:  ", NTP_STR),
	VDC_INIT("bogusorg",	"bogus origin:        ", NTP_STR),
	VDC_INIT("oldpkt",	"duplicate:           ", NTP_STR),
	VDC_INIT("seldisp",	"bad dispersion:      ", NTP_STR),
	VDC_INIT("selbroken",	"bad reference time:  ", NTP_STR),
	VDC_INIT("candidate",	"candidate order:     ", NTP_STR),
	VDC_INIT(NULL,		NULL,			 0)
    };
	associd_t associd;

	associd = checkassocid(pcmd->argval[0].uval);
	if (0 == associd)
		return;

	collect_display_vdc(associd, pstats_vdc, TRUE, fp);
}
