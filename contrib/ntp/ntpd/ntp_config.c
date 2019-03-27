/* ntp_config.c
 *
 * This file contains the ntpd configuration code.
 *
 * Written By:	Sachin Kamboj
 *		University of Delaware
 *		Newark, DE 19711
 * Some parts borrowed from the older ntp_config.c
 * Copyright (c) 2006
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#ifdef HAVE_NETINFO
# include <netinfo/ni.h>
#endif

#include <stdio.h>
#include <ctype.h>
#ifdef HAVE_SYS_PARAM_H
# include <sys/param.h>
#endif
#include <signal.h>
#ifndef SIGCHLD
# define SIGCHLD SIGCLD
#endif
#ifdef HAVE_SYS_WAIT_H
# include <sys/wait.h>
#endif

#include <isc/net.h>
#include <isc/result.h>

#include "ntp.h"
#include "ntpd.h"
#include "ntp_io.h"
#include "ntp_unixtime.h"
#include "ntp_refclock.h"
#include "ntp_filegen.h"
#include "ntp_stdlib.h"
#include "lib_strbuf.h"
#include "ntp_assert.h"
#include "ntp_random.h"
/*
 * [Bug 467]: Some linux headers collide with CONFIG_PHONE and CONFIG_KEYS
 * so #include these later.
 */
#include "ntp_config.h"
#include "ntp_cmdargs.h"
#include "ntp_scanner.h"
#include "ntp_parser.h"
#include "ntpd-opts.h"

#ifndef IGNORE_DNS_ERRORS
# define DNSFLAGS 0
#else
# define DNSFLAGS GAIR_F_IGNDNSERR
#endif

extern int yyparse(void);

/* Bug 2817 */
#if defined(HAVE_SYS_MMAN_H)
# include <sys/mman.h>
#endif

/* list of servers from command line for config_peers() */
int	cmdline_server_count;
char **	cmdline_servers;

/* Current state of memory locking:
 * -1: default
 *  0: memory locking disabled
 *  1: Memory locking enabled
 */
int	cur_memlock = -1;

/*
 * "logconfig" building blocks
 */
struct masks {
	const char * const	name;
	const u_int32		mask;
};

static struct masks logcfg_class[] = {
	{ "clock",	NLOG_OCLOCK },
	{ "peer",	NLOG_OPEER },
	{ "sync",	NLOG_OSYNC },
	{ "sys",	NLOG_OSYS },
	{ NULL,		0 }
};

/* logcfg_noclass_items[] masks are complete and must not be shifted */
static struct masks logcfg_noclass_items[] = {
	{ "allall",		NLOG_SYSMASK | NLOG_PEERMASK | NLOG_CLOCKMASK | NLOG_SYNCMASK },
	{ "allinfo",		NLOG_SYSINFO | NLOG_PEERINFO | NLOG_CLOCKINFO | NLOG_SYNCINFO },
	{ "allevents",		NLOG_SYSEVENT | NLOG_PEEREVENT | NLOG_CLOCKEVENT | NLOG_SYNCEVENT },
	{ "allstatus",		NLOG_SYSSTATUS | NLOG_PEERSTATUS | NLOG_CLOCKSTATUS | NLOG_SYNCSTATUS },
	{ "allstatistics",	NLOG_SYSSTATIST | NLOG_PEERSTATIST | NLOG_CLOCKSTATIST | NLOG_SYNCSTATIST },
	/* the remainder are misspellings of clockall, peerall, sysall, and syncall. */
	{ "allclock",		(NLOG_INFO | NLOG_STATIST | NLOG_EVENT | NLOG_STATUS) << NLOG_OCLOCK },
	{ "allpeer",		(NLOG_INFO | NLOG_STATIST | NLOG_EVENT | NLOG_STATUS) << NLOG_OPEER },
	{ "allsys",		(NLOG_INFO | NLOG_STATIST | NLOG_EVENT | NLOG_STATUS) << NLOG_OSYS },
	{ "allsync",		(NLOG_INFO | NLOG_STATIST | NLOG_EVENT | NLOG_STATUS) << NLOG_OSYNC },
	{ NULL,			0 }
};

/* logcfg_class_items[] masks are shiftable by NLOG_O* counts */
static struct masks logcfg_class_items[] = {
	{ "all",		NLOG_INFO | NLOG_EVENT | NLOG_STATUS | NLOG_STATIST },
	{ "info",		NLOG_INFO },
	{ "events",		NLOG_EVENT },
	{ "status",		NLOG_STATUS },
	{ "statistics",		NLOG_STATIST },
	{ NULL,			0 }
};

typedef struct peer_resolved_ctx_tag {
	int		flags;
	int		host_mode;	/* T_* token identifier */
	u_short		family;
	keyid_t		keyid;
	u_char		hmode;		/* MODE_* */
	u_char		version;
	u_char		minpoll;
	u_char		maxpoll;
	u_int32		ttl;
	const char *	group;
} peer_resolved_ctx;

/* Limits */
#define MAXPHONE	10	/* maximum number of phone strings */
#define MAXPPS		20	/* maximum length of PPS device string */

/*
 * Miscellaneous macros
 */
#define ISEOL(c)	((c) == '#' || (c) == '\n' || (c) == '\0')
#define ISSPACE(c)	((c) == ' ' || (c) == '\t')

#define _UC(str)	((char *)(intptr_t)(str))

/*
 * Definitions of things either imported from or exported to outside
 */
extern int yydebug;			/* ntp_parser.c (.y) */
config_tree cfgt;			/* Parser output stored here */
struct config_tree_tag *cfg_tree_history;	/* History of configs */
char *	sys_phone[MAXPHONE] = {NULL};	/* ACTS phone numbers */
char	default_keysdir[] = NTP_KEYSDIR;
char *	keysdir = default_keysdir;	/* crypto keys directory */
char *	saveconfigdir;
#if defined(HAVE_SCHED_SETSCHEDULER)
int	config_priority_override = 0;
int	config_priority;
#endif

const char *config_file;
static char default_ntp_signd_socket[] =
#ifdef NTP_SIGND_PATH
					NTP_SIGND_PATH;
#else
					"";
#endif
char *ntp_signd_socket = default_ntp_signd_socket;
#ifdef HAVE_NETINFO
struct netinfo_config_state *config_netinfo = NULL;
int check_netinfo = 1;
#endif /* HAVE_NETINFO */
#ifdef SYS_WINNT
char *alt_config_file;
LPTSTR temp;
char config_file_storage[MAX_PATH];
char alt_config_file_storage[MAX_PATH];
#endif /* SYS_WINNT */

#ifdef HAVE_NETINFO
/*
 * NetInfo configuration state
 */
struct netinfo_config_state {
	void *domain;		/* domain with config */
	ni_id config_dir;	/* ID config dir      */
	int prop_index;		/* current property   */
	int val_index;		/* current value      */
	char **val_list;	/* value list         */
};
#endif

struct REMOTE_CONFIG_INFO remote_config;  /* Remote configuration buffer and
					     pointer info */
int old_config_style = 1;    /* A boolean flag, which when set,
			      * indicates that the old configuration
			      * format with a newline at the end of
			      * every command is being used
			      */
int	cryptosw;		/* crypto command called */

extern char *stats_drift_file;	/* name of the driftfile */

#ifdef BC_LIST_FRAMEWORK_NOT_YET_USED
/*
 * backwards compatibility flags
 */
bc_entry bc_list[] = {
	{ T_Bc_bugXXXX,		1	}	/* default enabled */
};

/*
 * declare an int pointer for each flag for quick testing without
 * walking bc_list.  If the pointer is consumed by libntp rather
 * than ntpd, declare it in a libntp source file pointing to storage
 * initialized with the appropriate value for other libntp clients, and
 * redirect it to point into bc_list during ntpd startup.
 */
int *p_bcXXXX_enabled = &bc_list[0].enabled;
#endif

/* FUNCTION PROTOTYPES */

static void init_syntax_tree(config_tree *);
static void apply_enable_disable(attr_val_fifo *q, int enable);

#ifdef FREE_CFG_T
static void free_auth_node(config_tree *);
static void free_all_config_trees(void);

static void free_config_access(config_tree *);
static void free_config_auth(config_tree *);
static void free_config_fudge(config_tree *);
static void free_config_logconfig(config_tree *);
static void free_config_monitor(config_tree *);
static void free_config_nic_rules(config_tree *);
static void free_config_other_modes(config_tree *);
static void free_config_peers(config_tree *);
static void free_config_phone(config_tree *);
static void free_config_reset_counters(config_tree *);
static void free_config_rlimit(config_tree *);
static void free_config_setvar(config_tree *);
static void free_config_system_opts(config_tree *);
static void free_config_tinker(config_tree *);
static void free_config_tos(config_tree *);
static void free_config_trap(config_tree *);
static void free_config_ttl(config_tree *);
static void free_config_unpeers(config_tree *);
static void free_config_vars(config_tree *);

#ifdef SIM
static void free_config_sim(config_tree *);
#endif
static void destroy_address_fifo(address_fifo *);
#define FREE_ADDRESS_FIFO(pf)			\
	do {					\
		destroy_address_fifo(pf);	\
		(pf) = NULL;			\
	} while (0)
       void free_all_config_trees(void);	/* atexit() */
static void free_config_tree(config_tree *ptree);
#endif	/* FREE_CFG_T */

static void destroy_restrict_node(restrict_node *my_node);
static int is_sane_resolved_address(sockaddr_u *peeraddr, int hmode);
static void save_and_apply_config_tree(int/*BOOL*/ from_file);
static void destroy_int_fifo(int_fifo *);
#define FREE_INT_FIFO(pf)			\
	do {					\
		destroy_int_fifo(pf);		\
		(pf) = NULL;			\
	} while (0)
static void destroy_string_fifo(string_fifo *);
#define FREE_STRING_FIFO(pf)			\
	do {					\
		destroy_string_fifo(pf);		\
		(pf) = NULL;			\
	} while (0)
static void destroy_attr_val_fifo(attr_val_fifo *);
#define FREE_ATTR_VAL_FIFO(pf)			\
	do {					\
		destroy_attr_val_fifo(pf);	\
		(pf) = NULL;			\
	} while (0)
static void destroy_filegen_fifo(filegen_fifo *);
#define FREE_FILEGEN_FIFO(pf)			\
	do {					\
		destroy_filegen_fifo(pf);	\
		(pf) = NULL;			\
	} while (0)
static void destroy_restrict_fifo(restrict_fifo *);
#define FREE_RESTRICT_FIFO(pf)			\
	do {					\
		destroy_restrict_fifo(pf);	\
		(pf) = NULL;			\
	} while (0)
static void destroy_setvar_fifo(setvar_fifo *);
#define FREE_SETVAR_FIFO(pf)			\
	do {					\
		destroy_setvar_fifo(pf);	\
		(pf) = NULL;			\
	} while (0)
static void destroy_addr_opts_fifo(addr_opts_fifo *);
#define FREE_ADDR_OPTS_FIFO(pf)			\
	do {					\
		destroy_addr_opts_fifo(pf);	\
		(pf) = NULL;			\
	} while (0)

static void config_logconfig(config_tree *);
static void config_monitor(config_tree *);
static void config_rlimit(config_tree *);
static void config_system_opts(config_tree *);
static void config_tinker(config_tree *);
static int  config_tos_clock(config_tree *);
static void config_tos(config_tree *);
static void config_vars(config_tree *);

#ifdef SIM
static sockaddr_u *get_next_address(address_node *addr);
static void config_sim(config_tree *);
static void config_ntpdsim(config_tree *);
#else	/* !SIM follows */
static void config_ntpd(config_tree *, int/*BOOL*/ input_from_file);
static void config_other_modes(config_tree *);
static void config_auth(config_tree *);
static void config_access(config_tree *);
static void config_mdnstries(config_tree *);
static void config_phone(config_tree *);
static void config_setvar(config_tree *);
static void config_ttl(config_tree *);
static void config_trap(config_tree *);
static void config_fudge(config_tree *);
static void config_peers(config_tree *);
static void config_unpeers(config_tree *);
static void config_nic_rules(config_tree *, int/*BOOL*/ input_from_file);
static void config_reset_counters(config_tree *);
static u_char get_correct_host_mode(int token);
static int peerflag_bits(peer_node *);
#endif	/* !SIM */

#ifdef WORKER
static void peer_name_resolved(int, int, void *, const char *, const char *,
			const struct addrinfo *,
			const struct addrinfo *);
static void unpeer_name_resolved(int, int, void *, const char *, const char *,
			  const struct addrinfo *,
			  const struct addrinfo *);
static void trap_name_resolved(int, int, void *, const char *, const char *,
			const struct addrinfo *,
			const struct addrinfo *);
#endif

enum gnn_type {
	t_UNK,		/* Unknown */
	t_REF,		/* Refclock */
	t_MSK		/* Network Mask */
};

static void ntpd_set_tod_using(const char *);
static char * normal_dtoa(double);
static u_int32 get_pfxmatch(const char **, struct masks *);
static u_int32 get_match(const char *, struct masks *);
static u_int32 get_logmask(const char *);
static int/*BOOL*/ is_refclk_addr(const address_node * addr);

static void	appendstr(char *, size_t, const char *);


#ifndef SIM
static int getnetnum(const char *num, sockaddr_u *addr, int complain,
		     enum gnn_type a_type);

#endif

#if defined(__GNUC__) /* this covers CLANG, too */
static void  __attribute__((noreturn,format(printf,1,2))) fatal_error(const char *fmt, ...)
#elif defined(_MSC_VER)
static void __declspec(noreturn) fatal_error(const char *fmt, ...)
#else
static void fatal_error(const char *fmt, ...)
#endif
{
	va_list va;

	va_start(va, fmt);
	mvsyslog(LOG_EMERG, fmt, va);
	va_end(va);
	_exit(1);
}


/* FUNCTIONS FOR INITIALIZATION
 * ----------------------------
 */

#ifdef FREE_CFG_T
static void
free_auth_node(
	config_tree *ptree
	)
{
	if (ptree->auth.keys) {
		free(ptree->auth.keys);
		ptree->auth.keys = NULL;
	}

	if (ptree->auth.keysdir) {
		free(ptree->auth.keysdir);
		ptree->auth.keysdir = NULL;
	}

	if (ptree->auth.ntp_signd_socket) {
		free(ptree->auth.ntp_signd_socket);
		ptree->auth.ntp_signd_socket = NULL;
	}
}
#endif /* DEBUG */


static void
init_syntax_tree(
	config_tree *ptree
	)
{
	ZERO(*ptree);
	ptree->mdnstries = 5;
}


#ifdef FREE_CFG_T
static void
free_all_config_trees(void)
{
	config_tree *ptree;
	config_tree *pnext;

	ptree = cfg_tree_history;

	while (ptree != NULL) {
		pnext = ptree->link;
		free_config_tree(ptree);
		ptree = pnext;
	}
}


static void
free_config_tree(
	config_tree *ptree
	)
{
#if defined(_MSC_VER) && defined (_DEBUG)
	_CrtCheckMemory();
#endif

	if (ptree->source.value.s != NULL)
		free(ptree->source.value.s);

	free_config_other_modes(ptree);
	free_config_auth(ptree);
	free_config_tos(ptree);
	free_config_monitor(ptree);
	free_config_access(ptree);
	free_config_tinker(ptree);
	free_config_rlimit(ptree);
	free_config_system_opts(ptree);
	free_config_logconfig(ptree);
	free_config_phone(ptree);
	free_config_setvar(ptree);
	free_config_ttl(ptree);
	free_config_trap(ptree);
	free_config_fudge(ptree);
	free_config_vars(ptree);
	free_config_peers(ptree);
	free_config_unpeers(ptree);
	free_config_nic_rules(ptree);
	free_config_reset_counters(ptree);
#ifdef SIM
	free_config_sim(ptree);
#endif
	free_auth_node(ptree);

	free(ptree);

#if defined(_MSC_VER) && defined (_DEBUG)
	_CrtCheckMemory();
#endif
}
#endif /* FREE_CFG_T */


#ifdef SAVECONFIG
/* Dump all trees */
int
dump_all_config_trees(
	FILE *df,
	int comment
	)
{
	config_tree *	cfg_ptr;
	int		return_value;

	return_value = 0;
	for (cfg_ptr = cfg_tree_history;
	     cfg_ptr != NULL;
	     cfg_ptr = cfg_ptr->link)
		return_value |= dump_config_tree(cfg_ptr, df, comment);

	return return_value;
}


/* The config dumper */
int
dump_config_tree(
	config_tree *ptree,
	FILE *df,
	int comment
	)
{
	peer_node *peern;
	unpeer_node *unpeern;
	attr_val *atrv;
	address_node *addr;
	address_node *peer_addr;
	address_node *fudge_addr;
	filegen_node *fgen_node;
	restrict_node *rest_node;
	addr_opts_node *addr_opts;
	setvar_node *setv_node;
	nic_rule_node *rule_node;
	int_node *i_n;
	int_node *flag_tok_fifo;
	int_node *counter_set;
	string_node *str_node;

	const char *s = NULL;
	char *s1;
	char *s2;
	char timestamp[80];
	int enable;

	DPRINTF(1, ("dump_config_tree(%p)\n", ptree));

	if (comment) {
		if (!strftime(timestamp, sizeof(timestamp),
			      "%Y-%m-%d %H:%M:%S",
			      localtime(&ptree->timestamp)))
			timestamp[0] = '\0';

		fprintf(df, "# %s %s %s\n",
			timestamp,
			(CONF_SOURCE_NTPQ == ptree->source.attr)
			    ? "ntpq remote config from"
			    : "startup configuration file",
			ptree->source.value.s);
	}

	/*
	 * For options without documentation we just output the name
	 * and its data value
	 */
	atrv = HEAD_PFIFO(ptree->vars);
	for ( ; atrv != NULL; atrv = atrv->link) {
		switch (atrv->type) {
#ifdef DEBUG
		default:
			fprintf(df, "\n# dump error:\n"
				"# unknown vars type %d (%s) for %s\n",
				atrv->type, token_name(atrv->type),
				token_name(atrv->attr));
			break;
#endif
		case T_Double:
			fprintf(df, "%s %s\n", keyword(atrv->attr),
				normal_dtoa(atrv->value.d));
			break;

		case T_Integer:
			fprintf(df, "%s %d\n", keyword(atrv->attr),
				atrv->value.i);
			break;

		case T_String:
			fprintf(df, "%s \"%s\"", keyword(atrv->attr),
				atrv->value.s);
			if (T_Driftfile == atrv->attr &&
			    atrv->link != NULL &&
			    T_WanderThreshold == atrv->link->attr) {
				atrv = atrv->link;
				fprintf(df, " %s\n",
					normal_dtoa(atrv->value.d));
			} else {
				fprintf(df, "\n");
			}
			break;
		}
	}

	atrv = HEAD_PFIFO(ptree->logconfig);
	if (atrv != NULL) {
		fprintf(df, "logconfig");
		for ( ; atrv != NULL; atrv = atrv->link)
			fprintf(df, " %c%s", atrv->attr, atrv->value.s);
		fprintf(df, "\n");
	}

	if (ptree->stats_dir)
		fprintf(df, "statsdir \"%s\"\n", ptree->stats_dir);

	i_n = HEAD_PFIFO(ptree->stats_list);
	if (i_n != NULL) {
		fprintf(df, "statistics");
		for ( ; i_n != NULL; i_n = i_n->link)
			fprintf(df, " %s", keyword(i_n->i));
		fprintf(df, "\n");
	}

	fgen_node = HEAD_PFIFO(ptree->filegen_opts);
	for ( ; fgen_node != NULL; fgen_node = fgen_node->link) {
		atrv = HEAD_PFIFO(fgen_node->options);
		if (atrv != NULL) {
			fprintf(df, "filegen %s",
				keyword(fgen_node->filegen_token));
			for ( ; atrv != NULL; atrv = atrv->link) {
				switch (atrv->attr) {
#ifdef DEBUG
				default:
					fprintf(df, "\n# dump error:\n"
						"# unknown filegen option token %s\n"
						"filegen %s",
						token_name(atrv->attr),
						keyword(fgen_node->filegen_token));
					break;
#endif
				case T_File:
					fprintf(df, " file %s",
						atrv->value.s);
					break;

				case T_Type:
					fprintf(df, " type %s",
						keyword(atrv->value.i));
					break;

				case T_Flag:
					fprintf(df, " %s",
						keyword(atrv->value.i));
					break;
				}
			}
			fprintf(df, "\n");
		}
	}

	atrv = HEAD_PFIFO(ptree->auth.crypto_cmd_list);
	if (atrv != NULL) {
		fprintf(df, "crypto");
		for ( ; atrv != NULL; atrv = atrv->link) {
			fprintf(df, " %s %s", keyword(atrv->attr),
				atrv->value.s);
		}
		fprintf(df, "\n");
	}

	if (ptree->auth.revoke != 0)
		fprintf(df, "revoke %d\n", ptree->auth.revoke);

	if (ptree->auth.keysdir != NULL)
		fprintf(df, "keysdir \"%s\"\n", ptree->auth.keysdir);

	if (ptree->auth.keys != NULL)
		fprintf(df, "keys \"%s\"\n", ptree->auth.keys);

	atrv = HEAD_PFIFO(ptree->auth.trusted_key_list);
	if (atrv != NULL) {
		fprintf(df, "trustedkey");
		for ( ; atrv != NULL; atrv = atrv->link) {
			if (T_Integer == atrv->type)
				fprintf(df, " %d", atrv->value.i);
			else if (T_Intrange == atrv->type)
				fprintf(df, " (%d ... %d)",
					atrv->value.r.first,
					atrv->value.r.last);
#ifdef DEBUG
			else
				fprintf(df, "\n# dump error:\n"
					"# unknown trustedkey attr type %d\n"
					"trustedkey", atrv->type);
#endif
		}
		fprintf(df, "\n");
	}

	if (ptree->auth.control_key)
		fprintf(df, "controlkey %d\n", ptree->auth.control_key);

	if (ptree->auth.request_key)
		fprintf(df, "requestkey %d\n", ptree->auth.request_key);

	/* dump enable list, then disable list */
	for (enable = 1; enable >= 0; enable--) {
		atrv = (enable)
			   ? HEAD_PFIFO(ptree->enable_opts)
			   : HEAD_PFIFO(ptree->disable_opts);
		if (atrv != NULL) {
			fprintf(df, "%s", (enable)
					? "enable"
					: "disable");
			for ( ; atrv != NULL; atrv = atrv->link)
				fprintf(df, " %s",
					keyword(atrv->value.i));
			fprintf(df, "\n");
		}
	}

	atrv = HEAD_PFIFO(ptree->orphan_cmds);
	if (atrv != NULL) {
		fprintf(df, "tos");
		for ( ; atrv != NULL; atrv = atrv->link) {
			switch (atrv->type) {
#ifdef DEBUG
			default:
				fprintf(df, "\n# dump error:\n"
					"# unknown tos attr type %d %s\n"
					"tos", atrv->type,
					token_name(atrv->type));
				break;
#endif
			case T_Integer:
				if (atrv->attr == T_Basedate) {
					struct calendar jd;
					ntpcal_rd_to_date(&jd, atrv->value.i + DAY_NTP_STARTS);
					fprintf(df, " %s \"%04hu-%02hu-%02hu\"",
						keyword(atrv->attr), jd.year,
						(u_short)jd.month,
						(u_short)jd.monthday);
				} else {
					fprintf(df, " %s %d",
					keyword(atrv->attr),
					atrv->value.i);
				}
				break;

			case T_Double:
				fprintf(df, " %s %s",
					keyword(atrv->attr),
					normal_dtoa(atrv->value.d));
				break;
			}
		}
		fprintf(df, "\n");
	}

	atrv = HEAD_PFIFO(ptree->rlimit);
	if (atrv != NULL) {
		fprintf(df, "rlimit");
		for ( ; atrv != NULL; atrv = atrv->link) {
			INSIST(T_Integer == atrv->type);
			fprintf(df, " %s %d", keyword(atrv->attr),
				atrv->value.i);
		}
		fprintf(df, "\n");
	}

	atrv = HEAD_PFIFO(ptree->tinker);
	if (atrv != NULL) {
		fprintf(df, "tinker");
		for ( ; atrv != NULL; atrv = atrv->link) {
			INSIST(T_Double == atrv->type);
			fprintf(df, " %s %s", keyword(atrv->attr),
				normal_dtoa(atrv->value.d));
		}
		fprintf(df, "\n");
	}

	if (ptree->broadcastclient)
		fprintf(df, "broadcastclient\n");

	peern = HEAD_PFIFO(ptree->peers);
	for ( ; peern != NULL; peern = peern->link) {
		addr = peern->addr;
		fprintf(df, "%s", keyword(peern->host_mode));
		switch (addr->type) {
#ifdef DEBUG
		default:
			fprintf(df, "# dump error:\n"
				"# unknown peer family %d for:\n"
				"%s", addr->type,
				keyword(peern->host_mode));
			break;
#endif
		case AF_UNSPEC:
			break;

		case AF_INET:
			fprintf(df, " -4");
			break;

		case AF_INET6:
			fprintf(df, " -6");
			break;
		}
		fprintf(df, " %s", addr->address);

		if (peern->minpoll != 0)
			fprintf(df, " minpoll %u", peern->minpoll);

		if (peern->maxpoll != 0)
			fprintf(df, " maxpoll %u", peern->maxpoll);

		if (peern->ttl != 0) {
			if (strlen(addr->address) > 8
			    && !memcmp(addr->address, "127.127.", 8))
				fprintf(df, " mode %u", peern->ttl);
			else
				fprintf(df, " ttl %u", peern->ttl);
		}

		if (peern->peerversion != NTP_VERSION)
			fprintf(df, " version %u", peern->peerversion);

		if (peern->peerkey != 0)
			fprintf(df, " key %u", peern->peerkey);

		if (peern->group != NULL)
			fprintf(df, " ident \"%s\"", peern->group);

		atrv = HEAD_PFIFO(peern->peerflags);
		for ( ; atrv != NULL; atrv = atrv->link) {
			INSIST(T_Flag == atrv->attr);
			INSIST(T_Integer == atrv->type);
			fprintf(df, " %s", keyword(atrv->value.i));
		}

		fprintf(df, "\n");

		addr_opts = HEAD_PFIFO(ptree->fudge);
		for ( ; addr_opts != NULL; addr_opts = addr_opts->link) {
			peer_addr = peern->addr;
			fudge_addr = addr_opts->addr;

			s1 = peer_addr->address;
			s2 = fudge_addr->address;

			if (strcmp(s1, s2))
				continue;

			fprintf(df, "fudge %s", s1);

			for (atrv = HEAD_PFIFO(addr_opts->options);
			     atrv != NULL;
			     atrv = atrv->link) {

				switch (atrv->type) {
#ifdef DEBUG
				default:
					fprintf(df, "\n# dump error:\n"
						"# unknown fudge atrv->type %d\n"
						"fudge %s", atrv->type,
						s1);
					break;
#endif
				case T_Double:
					fprintf(df, " %s %s",
						keyword(atrv->attr),
						normal_dtoa(atrv->value.d));
					break;

				case T_Integer:
					fprintf(df, " %s %d",
						keyword(atrv->attr),
						atrv->value.i);
					break;

				case T_String:
					fprintf(df, " %s %s",
						keyword(atrv->attr),
						atrv->value.s);
					break;
				}
			}
			fprintf(df, "\n");
		}
	}

	addr = HEAD_PFIFO(ptree->manycastserver);
	if (addr != NULL) {
		fprintf(df, "manycastserver");
		for ( ; addr != NULL; addr = addr->link)
			fprintf(df, " %s", addr->address);
		fprintf(df, "\n");
	}

	addr = HEAD_PFIFO(ptree->multicastclient);
	if (addr != NULL) {
		fprintf(df, "multicastclient");
		for ( ; addr != NULL; addr = addr->link)
			fprintf(df, " %s", addr->address);
		fprintf(df, "\n");
	}


	for (unpeern = HEAD_PFIFO(ptree->unpeers);
	     unpeern != NULL;
	     unpeern = unpeern->link)
		fprintf(df, "unpeer %s\n", unpeern->addr->address);

	atrv = HEAD_PFIFO(ptree->mru_opts);
	if (atrv != NULL) {
		fprintf(df, "mru");
		for ( ;	atrv != NULL; atrv = atrv->link)
			fprintf(df, " %s %d", keyword(atrv->attr),
				atrv->value.i);
		fprintf(df, "\n");
	}

	atrv = HEAD_PFIFO(ptree->discard_opts);
	if (atrv != NULL) {
		fprintf(df, "discard");
		for ( ;	atrv != NULL; atrv = atrv->link)
			fprintf(df, " %s %d", keyword(atrv->attr),
				atrv->value.i);
		fprintf(df, "\n");
	}

	for (rest_node = HEAD_PFIFO(ptree->restrict_opts);
	     rest_node != NULL;
	     rest_node = rest_node->link) {
		int is_default = 0;

		if (NULL == rest_node->addr) {
			s = "default";
			/* Don't need to set is_default=1 here */
			flag_tok_fifo = HEAD_PFIFO(rest_node->flag_tok_fifo);
			for ( ; flag_tok_fifo != NULL; flag_tok_fifo = flag_tok_fifo->link) {
				if (T_Source == flag_tok_fifo->i) {
					s = "source";
					break;
				}
			}
		} else {
			const char *ap = rest_node->addr->address;
			const char *mp = "";

			if (rest_node->mask)
				mp = rest_node->mask->address;

			if (   rest_node->addr->type == AF_INET
			    && !strcmp(ap, "0.0.0.0")
			    && !strcmp(mp, "0.0.0.0")) {
				is_default = 1;
				s = "-4 default";
			} else if (   rest_node->mask
				   && rest_node->mask->type == AF_INET6
				   && !strcmp(ap, "::")
				   && !strcmp(mp, "::")) {
				is_default = 1;
				s = "-6 default";
			} else {
				s = ap;
			}
		}
		fprintf(df, "restrict %s", s);
		if (rest_node->mask != NULL && !is_default)
			fprintf(df, " mask %s",
				rest_node->mask->address);
		fprintf(df, " ippeerlimit %d", rest_node->ippeerlimit);
		flag_tok_fifo = HEAD_PFIFO(rest_node->flag_tok_fifo);
		for ( ; flag_tok_fifo != NULL; flag_tok_fifo = flag_tok_fifo->link)
			if (T_Source != flag_tok_fifo->i)
				fprintf(df, " %s", keyword(flag_tok_fifo->i));
		fprintf(df, "\n");
	}

	rule_node = HEAD_PFIFO(ptree->nic_rules);
	for ( ; rule_node != NULL; rule_node = rule_node->link) {
		fprintf(df, "interface %s %s\n",
			keyword(rule_node->action),
			(rule_node->match_class)
			    ? keyword(rule_node->match_class)
			    : rule_node->if_name);
	}

	str_node = HEAD_PFIFO(ptree->phone);
	if (str_node != NULL) {
		fprintf(df, "phone");
		for ( ; str_node != NULL; str_node = str_node->link)
			fprintf(df, " \"%s\"", str_node->s);
		fprintf(df, "\n");
	}

	setv_node = HEAD_PFIFO(ptree->setvar);
	for ( ; setv_node != NULL; setv_node = setv_node->link) {
		s1 = quote_if_needed(setv_node->var);
		s2 = quote_if_needed(setv_node->val);
		fprintf(df, "setvar %s = %s", s1, s2);
		free(s1);
		free(s2);
		if (setv_node->isdefault)
			fprintf(df, " default");
		fprintf(df, "\n");
	}

	i_n = HEAD_PFIFO(ptree->ttl);
	if (i_n != NULL) {
		fprintf(df, "ttl");
		for( ; i_n != NULL; i_n = i_n->link)
			fprintf(df, " %d", i_n->i);
		fprintf(df, "\n");
	}

	addr_opts = HEAD_PFIFO(ptree->trap);
	for ( ; addr_opts != NULL; addr_opts = addr_opts->link) {
		addr = addr_opts->addr;
		fprintf(df, "trap %s", addr->address);
		atrv = HEAD_PFIFO(addr_opts->options);
		for ( ; atrv != NULL; atrv = atrv->link) {
			switch (atrv->attr) {
#ifdef DEBUG
			default:
				fprintf(df, "\n# dump error:\n"
					"# unknown trap token %d\n"
					"trap %s", atrv->attr,
					addr->address);
				break;
#endif
			case T_Port:
				fprintf(df, " port %d", atrv->value.i);
				break;

			case T_Interface:
				fprintf(df, " interface %s",
					atrv->value.s);
				break;
			}
		}
		fprintf(df, "\n");
	}

	counter_set = HEAD_PFIFO(ptree->reset_counters);
	if (counter_set != NULL) {
		fprintf(df, "reset");
		for ( ; counter_set != NULL;
		     counter_set = counter_set->link)
			fprintf(df, " %s", keyword(counter_set->i));
		fprintf(df, "\n");
	}

	return 0;
}
#endif	/* SAVECONFIG */



/* generic fifo routines for structs linked by 1st member */
void *
append_gen_fifo(
	void *fifo,
	void *entry
	)
{
	gen_fifo *pf;
	gen_node *pe;

	pf = fifo;
	pe = entry;
	if (NULL == pf)
		pf = emalloc_zero(sizeof(*pf));
	else
		CHECK_FIFO_CONSISTENCY(*pf);
	if (pe != NULL)
		LINK_FIFO(*pf, pe, link);
	CHECK_FIFO_CONSISTENCY(*pf);

	return pf;
}


void *
concat_gen_fifos(
	void *first,
	void *second
	)
{
	gen_fifo *pf1;
	gen_fifo *pf2;

	pf1 = first;
	pf2 = second;
	if (NULL == pf1)
		return pf2;
	if (NULL == pf2)
		return pf1;

	CONCAT_FIFO(*pf1, *pf2, link);
	free(pf2);

	return pf1;
}

void*
destroy_gen_fifo(
	void        *fifo,
	fifo_deleter func
	)
{
	any_node *	np  = NULL;
	any_node_fifo *	pf1 = fifo;

	if (pf1 != NULL) {
		if (!func)
			func = free;
		for (;;) {
			UNLINK_FIFO(np, *pf1, link);
			if (np == NULL)
				break;
			(*func)(np);
		}
		free(pf1);
	}
	return NULL;
}

/* FUNCTIONS FOR CREATING NODES ON THE SYNTAX TREE
 * -----------------------------------------------
 */

void
destroy_attr_val(
	attr_val *	av
	)
{
	if (av) {
		if (T_String == av->type)
			free(av->value.s);
		free(av);
	}
}

attr_val *
create_attr_dval(
	int attr,
	double value
	)
{
	attr_val *my_val;

	my_val = emalloc_zero(sizeof(*my_val));
	my_val->attr = attr;
	my_val->value.d = value;
	my_val->type = T_Double;

	return my_val;
}


attr_val *
create_attr_ival(
	int attr,
	int value
	)
{
	attr_val *my_val;

	my_val = emalloc_zero(sizeof(*my_val));
	my_val->attr = attr;
	my_val->value.i = value;
	my_val->type = T_Integer;

	return my_val;
}


attr_val *
create_attr_uval(
	int	attr,
	u_int	value
	)
{
	attr_val *my_val;

	my_val = emalloc_zero(sizeof(*my_val));
	my_val->attr = attr;
	my_val->value.u = value;
	my_val->type = T_U_int;

	return my_val;
}


attr_val *
create_attr_rangeval(
	int	attr,
	int	first,
	int	last
	)
{
	attr_val *my_val;

	my_val = emalloc_zero(sizeof(*my_val));
	my_val->attr = attr;
	my_val->value.r.first = first;
	my_val->value.r.last = last;
	my_val->type = T_Intrange;

	return my_val;
}


attr_val *
create_attr_sval(
	int attr,
	const char *s
	)
{
	attr_val *my_val;

	my_val = emalloc_zero(sizeof(*my_val));
	my_val->attr = attr;
	if (NULL == s)			/* free() hates NULL */
		s = estrdup("");
	my_val->value.s = _UC(s);
	my_val->type = T_String;

	return my_val;
}


int_node *
create_int_node(
	int val
	)
{
	int_node *i_n;

	i_n = emalloc_zero(sizeof(*i_n));
	i_n->i = val;

	return i_n;
}


string_node *
create_string_node(
	char *str
	)
{
	string_node *sn;

	sn = emalloc_zero(sizeof(*sn));
	sn->s = str;

	return sn;
}


address_node *
create_address_node(
	char *	addr,
	int	type
	)
{
	address_node *my_node;

	REQUIRE(NULL != addr);
	REQUIRE(AF_INET == type || AF_INET6 == type || AF_UNSPEC == type);
	my_node = emalloc_zero(sizeof(*my_node));
	my_node->address = addr;
	my_node->type = (u_short)type;

	return my_node;
}


void
destroy_address_node(
	address_node *my_node
	)
{
	if (NULL == my_node)
		return;
	REQUIRE(NULL != my_node->address);

	free(my_node->address);
	free(my_node);
}


peer_node *
create_peer_node(
	int		hmode,
	address_node *	addr,
	attr_val_fifo *	options
	)
{
	peer_node *my_node;
	attr_val *option;
	int freenode;
	int errflag = 0;

	my_node = emalloc_zero(sizeof(*my_node));

	/* Initialize node values to default */
	my_node->peerversion = NTP_VERSION;

	/* Now set the node to the read values */
	my_node->host_mode = hmode;
	my_node->addr = addr;

	/*
	 * the options FIFO mixes items that will be saved in the
	 * peer_node as explicit members, such as minpoll, and
	 * those that are moved intact to the peer_node's peerflags
	 * FIFO.  The options FIFO is consumed and reclaimed here.
	 */

	if (options != NULL)
		CHECK_FIFO_CONSISTENCY(*options);
	while (options != NULL) {
		UNLINK_FIFO(option, *options, link);
		if (NULL == option) {
			free(options);
			break;
		}

		freenode = 1;
		/* Check the kind of option being set */
		switch (option->attr) {

		case T_Flag:
			APPEND_G_FIFO(my_node->peerflags, option);
			freenode = 0;
			break;

		case T_Minpoll:
			if (option->value.i < NTP_MINPOLL ||
			    option->value.i > UCHAR_MAX) {
				msyslog(LOG_INFO,
					"minpoll: provided value (%d) is out of range [%d-%d])",
					option->value.i, NTP_MINPOLL,
					UCHAR_MAX);
				my_node->minpoll = NTP_MINPOLL;
			} else {
				my_node->minpoll =
					(u_char)option->value.u;
			}
			break;

		case T_Maxpoll:
			if (option->value.i < 0 ||
			    option->value.i > NTP_MAXPOLL) {
				msyslog(LOG_INFO,
					"maxpoll: provided value (%d) is out of range [0-%d])",
					option->value.i, NTP_MAXPOLL);
				my_node->maxpoll = NTP_MAXPOLL;
			} else {
				my_node->maxpoll =
					(u_char)option->value.u;
			}
			break;

		case T_Ttl:
			if (is_refclk_addr(addr)) {
				msyslog(LOG_ERR, "'ttl' does not apply for refclocks");
				errflag = 1;
			} else if (option->value.u >= MAX_TTL) {
				msyslog(LOG_ERR, "ttl: invalid argument");
				errflag = 1;
			} else {
				my_node->ttl = (u_char)option->value.u;
			}
			break;

		case T_Mode:
			if (is_refclk_addr(addr)) {
				my_node->ttl = option->value.u;
			} else {
				msyslog(LOG_ERR, "'mode' does not apply for network peers");
				errflag = 1;
			}
			break;

		case T_Key:
			if (option->value.u >= KEYID_T_MAX) {
				msyslog(LOG_ERR, "key: invalid argument");
				errflag = 1;
			} else {
				my_node->peerkey =
					(keyid_t)option->value.u;
			}
			break;

		case T_Version:
			if (option->value.u >= UCHAR_MAX) {
				msyslog(LOG_ERR, "version: invalid argument");
				errflag = 1;
			} else {
				my_node->peerversion =
					(u_char)option->value.u;
			}
			break;

		case T_Ident:
			my_node->group = option->value.s;
			break;

		default:
			msyslog(LOG_ERR,
				"Unknown peer/server option token %s",
				token_name(option->attr));
			errflag = 1;
		}
		if (freenode)
			free(option);
	}

	/* Check if errors were reported. If yes, ignore the node */
	if (errflag) {
		free(my_node);
		my_node = NULL;
	}

	return my_node;
}


unpeer_node *
create_unpeer_node(
	address_node *addr
	)
{
	unpeer_node *	my_node;
	u_long		u;
	const u_char *	pch;

	my_node = emalloc_zero(sizeof(*my_node));

	/*
	 * From the parser's perspective an association ID fits into
	 * its generic T_String definition of a name/address "address".
	 * We treat all valid 16-bit numbers as association IDs.
	 */
	for (u = 0, pch = (u_char*)addr->address; isdigit(*pch); ++pch) {
		/* accumulate with overflow retention */
		u = (10 * u + *pch - '0') | (u & 0xFF000000u);
	}

	if (!*pch && u <= ASSOCID_MAX) {
		my_node->assocID = (associd_t)u;
		my_node->addr = NULL;
		destroy_address_node(addr);
	} else {
		my_node->assocID = 0;
		my_node->addr = addr;
	}

	return my_node;
}

filegen_node *
create_filegen_node(
	int		filegen_token,
	attr_val_fifo *	options
	)
{
	filegen_node *my_node;

	my_node = emalloc_zero(sizeof(*my_node));
	my_node->filegen_token = filegen_token;
	my_node->options = options;

	return my_node;
}


restrict_node *
create_restrict_node(
	address_node *	addr,
	address_node *	mask,
	short		ippeerlimit,
	int_fifo *	flag_tok_fifo,
	int		line_no
	)
{
	restrict_node *my_node;

	my_node = emalloc_zero(sizeof(*my_node));
	my_node->addr = addr;
	my_node->mask = mask;
	my_node->ippeerlimit = ippeerlimit;
	my_node->flag_tok_fifo = flag_tok_fifo;
	my_node->line_no = line_no;

	return my_node;
}


static void
destroy_restrict_node(
	restrict_node *my_node
	)
{
	/* With great care, free all the memory occupied by
	 * the restrict node
	 */
	destroy_address_node(my_node->addr);
	destroy_address_node(my_node->mask);
	destroy_int_fifo(my_node->flag_tok_fifo);
	free(my_node);
}


static void
destroy_int_fifo(
	int_fifo *	fifo
	)
{
	int_node *	i_n;

	if (fifo != NULL) {
		for (;;) {
			UNLINK_FIFO(i_n, *fifo, link);
			if (i_n == NULL)
				break;
			free(i_n);
		}
		free(fifo);
	}
}


static void
destroy_string_fifo(
	string_fifo *	fifo
	)
{
	string_node *	sn;

	if (fifo != NULL) {
		for (;;) {
			UNLINK_FIFO(sn, *fifo, link);
			if (sn == NULL)
				break;
			free(sn->s);
			free(sn);
		}
		free(fifo);
	}
}


static void
destroy_attr_val_fifo(
	attr_val_fifo *	av_fifo
	)
{
	attr_val *	av;

	if (av_fifo != NULL) {
		for (;;) {
			UNLINK_FIFO(av, *av_fifo, link);
			if (av == NULL)
				break;
			destroy_attr_val(av);
		}
		free(av_fifo);
	}
}


static void
destroy_filegen_fifo(
	filegen_fifo *	fifo
	)
{
	filegen_node *	fg;

	if (fifo != NULL) {
		for (;;) {
			UNLINK_FIFO(fg, *fifo, link);
			if (fg == NULL)
				break;
			destroy_attr_val_fifo(fg->options);
			free(fg);
		}
		free(fifo);
	}
}


static void
destroy_restrict_fifo(
	restrict_fifo *	fifo
	)
{
	restrict_node *	rn;

	if (fifo != NULL) {
		for (;;) {
			UNLINK_FIFO(rn, *fifo, link);
			if (rn == NULL)
				break;
			destroy_restrict_node(rn);
		}
		free(fifo);
	}
}


static void
destroy_setvar_fifo(
	setvar_fifo *	fifo
	)
{
	setvar_node *	sv;

	if (fifo != NULL) {
		for (;;) {
			UNLINK_FIFO(sv, *fifo, link);
			if (sv == NULL)
				break;
			free(sv->var);
			free(sv->val);
			free(sv);
		}
		free(fifo);
	}
}


static void
destroy_addr_opts_fifo(
	addr_opts_fifo *	fifo
	)
{
	addr_opts_node *	aon;

	if (fifo != NULL) {
		for (;;) {
			UNLINK_FIFO(aon, *fifo, link);
			if (aon == NULL)
				break;
			destroy_address_node(aon->addr);
			destroy_attr_val_fifo(aon->options);
			free(aon);
		}
		free(fifo);
	}
}


setvar_node *
create_setvar_node(
	char *	var,
	char *	val,
	int	isdefault
	)
{
	setvar_node *	my_node;
	char *		pch;

	/* do not allow = in the variable name */
	pch = strchr(var, '=');
	if (NULL != pch)
		*pch = '\0';

	/* Now store the string into a setvar_node */
	my_node = emalloc_zero(sizeof(*my_node));
	my_node->var = var;
	my_node->val = val;
	my_node->isdefault = isdefault;

	return my_node;
}


nic_rule_node *
create_nic_rule_node(
	int match_class,
	char *if_name,	/* interface name or numeric address */
	int action
	)
{
	nic_rule_node *my_node;

	REQUIRE(match_class != 0 || if_name != NULL);

	my_node = emalloc_zero(sizeof(*my_node));
	my_node->match_class = match_class;
	my_node->if_name = if_name;
	my_node->action = action;

	return my_node;
}


addr_opts_node *
create_addr_opts_node(
	address_node *	addr,
	attr_val_fifo *	options
	)
{
	addr_opts_node *my_node;

	my_node = emalloc_zero(sizeof(*my_node));
	my_node->addr = addr;
	my_node->options = options;

	return my_node;
}


#ifdef SIM
script_info *
create_sim_script_info(
	double		duration,
	attr_val_fifo *	script_queue
	)
{
	script_info *my_info;
	attr_val *my_attr_val;

	my_info = emalloc_zero(sizeof(*my_info));

	/* Initialize Script Info with default values*/
	my_info->duration = duration;
	my_info->prop_delay = NET_DLY;
	my_info->proc_delay = PROC_DLY;

	/* Traverse the script_queue and fill out non-default values */

	for (my_attr_val = HEAD_PFIFO(script_queue);
	     my_attr_val != NULL;
	     my_attr_val = my_attr_val->link) {

		/* Set the desired value */
		switch (my_attr_val->attr) {

		case T_Freq_Offset:
			my_info->freq_offset = my_attr_val->value.d;
			break;

		case T_Wander:
			my_info->wander = my_attr_val->value.d;
			break;

		case T_Jitter:
			my_info->jitter = my_attr_val->value.d;
			break;

		case T_Prop_Delay:
			my_info->prop_delay = my_attr_val->value.d;
			break;

		case T_Proc_Delay:
			my_info->proc_delay = my_attr_val->value.d;
			break;

		default:
			msyslog(LOG_ERR, "Unknown script token %d",
				my_attr_val->attr);
		}
	}

	return my_info;
}
#endif	/* SIM */


#ifdef SIM
static sockaddr_u *
get_next_address(
	address_node *addr
	)
{
	const char addr_prefix[] = "192.168.0.";
	static int curr_addr_num = 1;
#define ADDR_LENGTH 16 + 1	/* room for 192.168.1.255 */
	char addr_string[ADDR_LENGTH];
	sockaddr_u *final_addr;
	struct addrinfo *ptr;
	int gai_err;

	final_addr = emalloc(sizeof(*final_addr));

	if (addr->type == T_String) {
		snprintf(addr_string, sizeof(addr_string), "%s%d",
			 addr_prefix, curr_addr_num++);
		printf("Selecting ip address %s for hostname %s\n",
		       addr_string, addr->address);
		gai_err = getaddrinfo(addr_string, "ntp", NULL, &ptr);
	} else {
		gai_err = getaddrinfo(addr->address, "ntp", NULL, &ptr);
	}

	if (gai_err) {
		fprintf(stderr, "ERROR!! Could not get a new address\n");
		exit(1);
	}
	memcpy(final_addr, ptr->ai_addr, ptr->ai_addrlen);
	fprintf(stderr, "Successful in setting ip address of simulated server to: %s\n",
		stoa(final_addr));
	freeaddrinfo(ptr);

	return final_addr;
}
#endif /* SIM */


#ifdef SIM
server_info *
create_sim_server(
	address_node *		addr,
	double			server_offset,
	script_info_fifo *	script
	)
{
	server_info *my_info;

	my_info = emalloc_zero(sizeof(*my_info));
	my_info->server_time = server_offset;
	my_info->addr = get_next_address(addr);
	my_info->script = script;
	UNLINK_FIFO(my_info->curr_script, *my_info->script, link);

	return my_info;
}
#endif	/* SIM */

sim_node *
create_sim_node(
	attr_val_fifo *		init_opts,
	server_info_fifo *	servers
	)
{
	sim_node *my_node;

	my_node = emalloc(sizeof(*my_node));
	my_node->init_opts = init_opts;
	my_node->servers = servers;

	return my_node;
}




/* FUNCTIONS FOR PERFORMING THE CONFIGURATION
 * ------------------------------------------
 */

#ifndef SIM
static void
config_other_modes(
	config_tree *	ptree
	)
{
	sockaddr_u	addr_sock;
	address_node *	addr_node;

	if (ptree->broadcastclient)
		proto_config(PROTO_BROADCLIENT, ptree->broadcastclient,
			     0., NULL);

	addr_node = HEAD_PFIFO(ptree->manycastserver);
	while (addr_node != NULL) {
		ZERO_SOCK(&addr_sock);
		AF(&addr_sock) = addr_node->type;
		if (1 == getnetnum(addr_node->address, &addr_sock, 1,
				   t_UNK)) {
			proto_config(PROTO_MULTICAST_ADD,
				     0, 0., &addr_sock);
			sys_manycastserver = 1;
		}
		addr_node = addr_node->link;
	}

	/* Configure the multicast clients */
	addr_node = HEAD_PFIFO(ptree->multicastclient);
	if (addr_node != NULL) {
		do {
			ZERO_SOCK(&addr_sock);
			AF(&addr_sock) = addr_node->type;
			if (1 == getnetnum(addr_node->address,
					   &addr_sock, 1, t_UNK)) {
				proto_config(PROTO_MULTICAST_ADD, 0, 0.,
					     &addr_sock);
			}
			addr_node = addr_node->link;
		} while (addr_node != NULL);
		proto_config(PROTO_MULTICAST_ADD, 1, 0., NULL);
	}
}
#endif	/* !SIM */


#ifdef FREE_CFG_T
static void
destroy_address_fifo(
	address_fifo *	pfifo
	)
{
	address_node *	addr_node;

	if (pfifo != NULL) {
		for (;;) {
			UNLINK_FIFO(addr_node, *pfifo, link);
			if (addr_node == NULL)
				break;
			destroy_address_node(addr_node);
		}
		free(pfifo);
	}
}


static void
free_config_other_modes(
	config_tree *ptree
	)
{
	FREE_ADDRESS_FIFO(ptree->manycastserver);
	FREE_ADDRESS_FIFO(ptree->multicastclient);
}
#endif	/* FREE_CFG_T */


#ifndef SIM
static void
config_auth(
	config_tree *ptree
	)
{
	attr_val *	my_val;
	int		first;
	int		last;
	int		i;
	int		count;
#ifdef AUTOKEY
	int		item;
#endif

	/* Crypto Command */
#ifdef AUTOKEY
	my_val = HEAD_PFIFO(ptree->auth.crypto_cmd_list);
	for (; my_val != NULL; my_val = my_val->link) {
		switch (my_val->attr) {

		default:
			fatal_error("config_auth: attr-token=%d", my_val->attr);

		case T_Host:
			item = CRYPTO_CONF_PRIV;
			break;

		case T_Ident:
			item = CRYPTO_CONF_IDENT;
			break;

		case T_Pw:
			item = CRYPTO_CONF_PW;
			break;

		case T_Randfile:
			item = CRYPTO_CONF_RAND;
			break;

		case T_Digest:
			item = CRYPTO_CONF_NID;
			break;
		}
		crypto_config(item, my_val->value.s);
	}
#endif	/* AUTOKEY */

	/* Keysdir Command */
	if (ptree->auth.keysdir) {
		if (keysdir != default_keysdir)
			free(keysdir);
		keysdir = estrdup(ptree->auth.keysdir);
	}


	/* ntp_signd_socket Command */
	if (ptree->auth.ntp_signd_socket) {
		if (ntp_signd_socket != default_ntp_signd_socket)
			free(ntp_signd_socket);
		ntp_signd_socket = estrdup(ptree->auth.ntp_signd_socket);
	}

#ifdef AUTOKEY
	if (ptree->auth.cryptosw && !cryptosw) {
		crypto_setup();
		cryptosw = 1;
	}
#endif	/* AUTOKEY */

	/*
	 * Count the number of trusted keys to preallocate storage and
	 * size the hash table.
	 */
	count = 0;
	my_val = HEAD_PFIFO(ptree->auth.trusted_key_list);
	for (; my_val != NULL; my_val = my_val->link) {
		if (T_Integer == my_val->type) {
			first = my_val->value.i;
			if (first > 1 && first <= NTP_MAXKEY)
				count++;
		} else {
			REQUIRE(T_Intrange == my_val->type);
			first = my_val->value.r.first;
			last = my_val->value.r.last;
			if (!(first > last || first < 1 ||
			    last > NTP_MAXKEY)) {
				count += 1 + last - first;
			}
		}
	}
	auth_prealloc_symkeys(count);

	/* Keys Command */
	if (ptree->auth.keys)
		getauthkeys(ptree->auth.keys);

	/* Control Key Command */
	if (ptree->auth.control_key)
		ctl_auth_keyid = (keyid_t)ptree->auth.control_key;

	/* Requested Key Command */
	if (ptree->auth.request_key) {
		DPRINTF(4, ("set info_auth_keyid to %08lx\n",
			    (u_long) ptree->auth.request_key));
		info_auth_keyid = (keyid_t)ptree->auth.request_key;
	}

	/* Trusted Key Command */
	my_val = HEAD_PFIFO(ptree->auth.trusted_key_list);
	for (; my_val != NULL; my_val = my_val->link) {
		if (T_Integer == my_val->type) {
			first = my_val->value.i;
			if (first >= 1 && first <= NTP_MAXKEY) {
				authtrust(first, TRUE);
			} else {
				msyslog(LOG_NOTICE,
					"Ignoring invalid trustedkey %d, min 1 max %d.",
					first, NTP_MAXKEY);
			}
		} else {
			first = my_val->value.r.first;
			last = my_val->value.r.last;
			if (first > last || first < 1 ||
			    last > NTP_MAXKEY) {
				msyslog(LOG_NOTICE,
					"Ignoring invalid trustedkey range %d ... %d, min 1 max %d.",
					first, last, NTP_MAXKEY);
			} else {
				for (i = first; i <= last; i++) {
					authtrust(i, TRUE);
				}
			}
		}
	}

#ifdef AUTOKEY
	/* crypto revoke command */
	if (ptree->auth.revoke > 2 && ptree->auth.revoke < 32)
		sys_revoke = (u_char)ptree->auth.revoke;
	else if (ptree->auth.revoke)
		msyslog(LOG_ERR,
			"'revoke' value %d ignored",
			ptree->auth.revoke);
#endif	/* AUTOKEY */
}
#endif	/* !SIM */


#ifdef FREE_CFG_T
static void
free_config_auth(
	config_tree *ptree
	)
{
	destroy_attr_val_fifo(ptree->auth.crypto_cmd_list);
	ptree->auth.crypto_cmd_list = NULL;
	destroy_attr_val_fifo(ptree->auth.trusted_key_list);
	ptree->auth.trusted_key_list = NULL;
}
#endif	/* FREE_CFG_T */


/* Configure low-level clock-related parameters. Return TRUE if the
 * clock might need adjustment like era-checking after the call, FALSE
 * otherwise.
 */
static int/*BOOL*/
config_tos_clock(
	config_tree *ptree
	)
{
	int		ret;
	attr_val *	tos;

	ret = FALSE;
	tos = HEAD_PFIFO(ptree->orphan_cmds);
	for (; tos != NULL; tos = tos->link) {
		switch(tos->attr) {

		default:
			break;

		case T_Basedate:
			basedate_set_day(tos->value.i);
			ret = TRUE;
			break;
		}
	}

	if (basedate_get_day() <= NTP_TO_UNIX_DAYS)
		basedate_set_day(basedate_eval_buildstamp() - 11);
	    
	return ret;
}

static void
config_tos(
	config_tree *ptree
	)
{
	attr_val *	tos;
	int		item;
	double		val;

	/* [Bug 2896] For the daemon to work properly it is essential
	 * that minsane < minclock <= maxclock.
	 *
	 * If either constraint is violated, the daemon will be or might
	 * become dysfunctional. Fixing the values is too fragile here,
	 * since three variables with interdependecies are involved. We
	 * just log an error but do not stop: This might be caused by
	 * remote config, and it might be fixed by remote config, too.
	 */
	int l_maxclock = sys_maxclock;
	int l_minclock = sys_minclock;
	int l_minsane  = sys_minsane;

	/* -*- phase one: inspect / sanitize the values */
	tos = HEAD_PFIFO(ptree->orphan_cmds);
	for (; tos != NULL; tos = tos->link) {
		/* not all attributes are doubles (any more), so loading
		 * 'val' in all cases is not a good idea: It should be
		 * done as needed in every case processed here.
		 */
		switch(tos->attr) {
		default:
			break;

		case T_Bcpollbstep:
			val = tos->value.d;
			if (val > 4) {
				msyslog(LOG_WARNING,
					"Using maximum bcpollbstep ceiling %d, %d requested",
					4, (int)val);
				tos->value.d = 4;
			} else if (val < 0) {
				msyslog(LOG_WARNING,
					"Using minimum bcpollbstep floor %d, %d requested",
					0, (int)val);
				tos->value.d = 0;
			}
			break;

		case T_Ceiling:
			val = tos->value.d;
			if (val > STRATUM_UNSPEC - 1) {
				msyslog(LOG_WARNING,
					"Using maximum tos ceiling %d, %d requested",
					STRATUM_UNSPEC - 1, (int)val);
				tos->value.d = STRATUM_UNSPEC - 1;
			} else if (val < 1) {
				msyslog(LOG_WARNING,
					"Using minimum tos floor %d, %d requested",
					1, (int)val);
				tos->value.d = 1;
			}
			break;

		case T_Minclock:
			val = tos->value.d;
			if ((int)tos->value.d < 1)
				tos->value.d = 1;
			l_minclock = (int)tos->value.d;
			break;

		case T_Maxclock:
			val = tos->value.d;
			if ((int)tos->value.d < 1)
				tos->value.d = 1;
			l_maxclock = (int)tos->value.d;
			break;

		case T_Minsane:
			val = tos->value.d;
			if ((int)tos->value.d < 0)
				tos->value.d = 0;
			l_minsane = (int)tos->value.d;
			break;
		}
	}

	if ( ! (l_minsane < l_minclock && l_minclock <= l_maxclock)) {
		msyslog(LOG_ERR,
			"tos error: must have minsane (%d) < minclock (%d) <= maxclock (%d)"
			" - daemon will not operate properly!",
			l_minsane, l_minclock, l_maxclock);
	}

	/* -*- phase two: forward the values to the protocol machinery */
	tos = HEAD_PFIFO(ptree->orphan_cmds);
	for (; tos != NULL; tos = tos->link) {
		switch(tos->attr) {

		default:
			fatal_error("config-tos: attr-token=%d", tos->attr);

		case T_Bcpollbstep:
			item = PROTO_BCPOLLBSTEP;
			break;

		case T_Ceiling:
			item = PROTO_CEILING;
			break;

		case T_Floor:
			item = PROTO_FLOOR;
			break;

		case T_Cohort:
			item = PROTO_COHORT;
			break;

		case T_Orphan:
			item = PROTO_ORPHAN;
			break;

		case T_Orphanwait:
			item = PROTO_ORPHWAIT;
			break;

		case T_Mindist:
			item = PROTO_MINDISP;
			break;

		case T_Maxdist:
			item = PROTO_MAXDIST;
			break;

		case T_Minclock:
			item = PROTO_MINCLOCK;
			break;

		case T_Maxclock:
			item = PROTO_MAXCLOCK;
			break;

		case T_Minsane:
			item = PROTO_MINSANE;
			break;

		case T_Beacon:
			item = PROTO_BEACON;
			break;

		case T_Basedate:
			continue; /* SKIP proto-config for this! */
		}
		proto_config(item, 0, tos->value.d, NULL);
	}
}


#ifdef FREE_CFG_T
static void
free_config_tos(
	config_tree *ptree
	)
{
	FREE_ATTR_VAL_FIFO(ptree->orphan_cmds);
}
#endif	/* FREE_CFG_T */


static void
config_monitor(
	config_tree *ptree
	)
{
	int_node *pfilegen_token;
	const char *filegen_string;
	const char *filegen_file;
	FILEGEN *filegen;
	filegen_node *my_node;
	attr_val *my_opts;
	int filegen_type;
	int filegen_flag;

	/* Set the statistics directory */
	if (ptree->stats_dir)
		stats_config(STATS_STATSDIR, ptree->stats_dir);

	/* NOTE:
	 * Calling filegen_get is brain dead. Doing a string
	 * comparison to find the relavant filegen structure is
	 * expensive.
	 *
	 * Through the parser, we already know which filegen is
	 * being specified. Hence, we should either store a
	 * pointer to the specified structure in the syntax tree
	 * or an index into a filegen array.
	 *
	 * Need to change the filegen code to reflect the above.
	 */

	/* Turn on the specified statistics */
	pfilegen_token = HEAD_PFIFO(ptree->stats_list);
	for (; pfilegen_token != NULL; pfilegen_token = pfilegen_token->link) {
		filegen_string = keyword(pfilegen_token->i);
		filegen = filegen_get(filegen_string);
		if (NULL == filegen) {
			msyslog(LOG_ERR,
				"stats %s unrecognized",
				filegen_string);
			continue;
		}
		DPRINTF(4, ("enabling filegen for %s statistics '%s%s'\n",
			    filegen_string, filegen->dir,
			    filegen->fname));
		filegen_flag = filegen->flag;
		filegen_flag |= FGEN_FLAG_ENABLED;
		filegen_config(filegen, statsdir, filegen_string,
			       filegen->type, filegen_flag);
	}

	/* Configure the statistics with the options */
	my_node = HEAD_PFIFO(ptree->filegen_opts);
	for (; my_node != NULL; my_node = my_node->link) {
		filegen_string = keyword(my_node->filegen_token);
		filegen = filegen_get(filegen_string);
		if (NULL == filegen) {
			msyslog(LOG_ERR,
				"filegen category '%s' unrecognized",
				filegen_string);
			continue;
		}
		filegen_file = filegen_string;

		/* Initialize the filegen variables to their pre-configuration states */
		filegen_flag = filegen->flag;
		filegen_type = filegen->type;

		/* "filegen ... enabled" is the default (when filegen is used) */
		filegen_flag |= FGEN_FLAG_ENABLED;

		my_opts = HEAD_PFIFO(my_node->options);
		for (; my_opts != NULL; my_opts = my_opts->link) {
			switch (my_opts->attr) {

			case T_File:
				filegen_file = my_opts->value.s;
				break;

			case T_Type:
				switch (my_opts->value.i) {

				default:
					fatal_error("config-monitor: type-token=%d", my_opts->value.i);

				case T_None:
					filegen_type = FILEGEN_NONE;
					break;

				case T_Pid:
					filegen_type = FILEGEN_PID;
					break;

				case T_Day:
					filegen_type = FILEGEN_DAY;
					break;

				case T_Week:
					filegen_type = FILEGEN_WEEK;
					break;

				case T_Month:
					filegen_type = FILEGEN_MONTH;
					break;

				case T_Year:
					filegen_type = FILEGEN_YEAR;
					break;

				case T_Age:
					filegen_type = FILEGEN_AGE;
					break;
				}
				break;

			case T_Flag:
				switch (my_opts->value.i) {

				case T_Link:
					filegen_flag |= FGEN_FLAG_LINK;
					break;

				case T_Nolink:
					filegen_flag &= ~FGEN_FLAG_LINK;
					break;

				case T_Enable:
					filegen_flag |= FGEN_FLAG_ENABLED;
					break;

				case T_Disable:
					filegen_flag &= ~FGEN_FLAG_ENABLED;
					break;

				default:
					msyslog(LOG_ERR,
						"Unknown filegen flag token %d",
						my_opts->value.i);
					exit(1);
				}
				break;

			default:
				msyslog(LOG_ERR,
					"Unknown filegen option token %d",
					my_opts->attr);
				exit(1);
			}
		}
		filegen_config(filegen, statsdir, filegen_file,
			       filegen_type, filegen_flag);
	}
}


#ifdef FREE_CFG_T
static void
free_config_monitor(
	config_tree *ptree
	)
{
	if (ptree->stats_dir) {
		free(ptree->stats_dir);
		ptree->stats_dir = NULL;
	}

	FREE_INT_FIFO(ptree->stats_list);
	FREE_FILEGEN_FIFO(ptree->filegen_opts);
}
#endif	/* FREE_CFG_T */


#ifndef SIM
static void
config_access(
	config_tree *ptree
	)
{
	static int		warned_signd;
	attr_val *		my_opt;
	restrict_node *		my_node;
	int_node *		curr_tok_fifo;
	sockaddr_u		addr;
	sockaddr_u		mask;
	struct addrinfo		hints;
	struct addrinfo *	ai_list;
	struct addrinfo *	pai;
	int			rc;
	int			restrict_default;
	u_short			rflags;
	u_short			mflags;
	short			ippeerlimit;
	int			range_err;
	const char *		signd_warning =
#ifdef HAVE_NTP_SIGND
	    "MS-SNTP signd operations currently block ntpd degrading service to all clients.";
#else
	    "mssntp restrict bit ignored, this ntpd was configured without --enable-ntp-signd.";
#endif

	/* Configure the mru options */
	my_opt = HEAD_PFIFO(ptree->mru_opts);
	for (; my_opt != NULL; my_opt = my_opt->link) {

		range_err = FALSE;

		switch (my_opt->attr) {

		case T_Incalloc:
			if (0 <= my_opt->value.i)
				mru_incalloc = my_opt->value.u;
			else
				range_err = TRUE;
			break;

		case T_Incmem:
			if (0 <= my_opt->value.i)
				mru_incalloc = (my_opt->value.u * 1024U)
						/ sizeof(mon_entry);
			else
				range_err = TRUE;
			break;

		case T_Initalloc:
			if (0 <= my_opt->value.i)
				mru_initalloc = my_opt->value.u;
			else
				range_err = TRUE;
			break;

		case T_Initmem:
			if (0 <= my_opt->value.i)
				mru_initalloc = (my_opt->value.u * 1024U)
						 / sizeof(mon_entry);
			else
				range_err = TRUE;
			break;

		case T_Mindepth:
			if (0 <= my_opt->value.i)
				mru_mindepth = my_opt->value.u;
			else
				range_err = TRUE;
			break;

		case T_Maxage:
			mru_maxage = my_opt->value.i;
			break;

		case T_Maxdepth:
			if (0 <= my_opt->value.i)
				mru_maxdepth = my_opt->value.u;
			else
				mru_maxdepth = UINT_MAX;
			break;

		case T_Maxmem:
			if (0 <= my_opt->value.i)
				mru_maxdepth = (my_opt->value.u * 1024U) /
					       sizeof(mon_entry);
			else
				mru_maxdepth = UINT_MAX;
			break;

		default:
			msyslog(LOG_ERR,
				"Unknown mru option %s (%d)",
				keyword(my_opt->attr), my_opt->attr);
			exit(1);
		}
		if (range_err)
			msyslog(LOG_ERR,
				"mru %s %d out of range, ignored.",
				keyword(my_opt->attr), my_opt->value.i);
	}

	/* Configure the discard options */
	my_opt = HEAD_PFIFO(ptree->discard_opts);
	for (; my_opt != NULL; my_opt = my_opt->link) {

		switch (my_opt->attr) {

		case T_Average:
			if (0 <= my_opt->value.i &&
			    my_opt->value.i <= UCHAR_MAX)
				ntp_minpoll = (u_char)my_opt->value.u;
			else
				msyslog(LOG_ERR,
					"discard average %d out of range, ignored.",
					my_opt->value.i);
			break;

		case T_Minimum:
			ntp_minpkt = my_opt->value.i;
			break;

		case T_Monitor:
			mon_age = my_opt->value.i;
			break;

		default:
			msyslog(LOG_ERR,
				"Unknown discard option %s (%d)",
				keyword(my_opt->attr), my_opt->attr);
			exit(1);
		}
	}

	/* Configure the restrict options */
	my_node = HEAD_PFIFO(ptree->restrict_opts);

	for (; my_node != NULL; my_node = my_node->link) {
		/* Grab the ippeerlmit */
		ippeerlimit = my_node->ippeerlimit;

DPRINTF(1, ("config_access: top-level node %p: ippeerlimit %d\n", my_node, ippeerlimit));

		/* Parse the flags */
		rflags = 0;
		mflags = 0;

		curr_tok_fifo = HEAD_PFIFO(my_node->flag_tok_fifo);
		for (; curr_tok_fifo != NULL; curr_tok_fifo = curr_tok_fifo->link) {
			switch (curr_tok_fifo->i) {

			default:
				fatal_error("config_access: flag-type-token=%d", curr_tok_fifo->i);

			case T_Ntpport:
				mflags |= RESM_NTPONLY;
				break;

			case T_Source:
				mflags |= RESM_SOURCE;
				break;

			case T_Flake:
				rflags |= RES_FLAKE;
				break;

			case T_Ignore:
				rflags |= RES_IGNORE;
				break;

			case T_Kod:
				rflags |= RES_KOD;
				break;

			case T_Mssntp:
				rflags |= RES_MSSNTP;
				break;

			case T_Limited:
				rflags |= RES_LIMITED;
				break;

			case T_Lowpriotrap:
				rflags |= RES_LPTRAP;
				break;

			case T_Nomodify:
				rflags |= RES_NOMODIFY;
				break;

			case T_Nomrulist:
				rflags |= RES_NOMRULIST;
				break;

			case T_Noepeer:
				rflags |= RES_NOEPEER;
				break;

			case T_Nopeer:
				rflags |= RES_NOPEER;
				break;

			case T_Noquery:
				rflags |= RES_NOQUERY;
				break;

			case T_Noserve:
				rflags |= RES_DONTSERVE;
				break;

			case T_Notrap:
				rflags |= RES_NOTRAP;
				break;

			case T_Notrust:
				rflags |= RES_DONTTRUST;
				break;

			case T_Version:
				rflags |= RES_VERSION;
				break;
			}
		}

		if ((RES_MSSNTP & rflags) && !warned_signd) {
			warned_signd = 1;
			fprintf(stderr, "%s\n", signd_warning);
			msyslog(LOG_WARNING, "%s", signd_warning);
		}

		/* It would be swell if we could identify the line number */
		if ((RES_KOD & rflags) && !(RES_LIMITED & rflags)) {
			const char *kod_where = (my_node->addr)
					  ? my_node->addr->address
					  : (mflags & RESM_SOURCE)
					    ? "source"
					    : "default";
			const char *kod_warn = "KOD does nothing without LIMITED.";

			fprintf(stderr, "restrict %s: %s\n", kod_where, kod_warn);
			msyslog(LOG_WARNING, "restrict %s: %s", kod_where, kod_warn);
		}

		ZERO_SOCK(&addr);
		ai_list = NULL;
		pai = NULL;
		restrict_default = 0;

		if (NULL == my_node->addr) {
			ZERO_SOCK(&mask);
			if (!(RESM_SOURCE & mflags)) {
				/*
				 * The user specified a default rule
				 * without a -4 / -6 qualifier, add to
				 * both lists
				 */
				restrict_default = 1;
			} else {
				/* apply "restrict source ..." */
				DPRINTF(1, ("restrict source template ippeerlimit %d mflags %x rflags %x\n",
					ippeerlimit, mflags, rflags));
				hack_restrict(RESTRICT_FLAGS, NULL, NULL,
					      ippeerlimit, mflags, rflags, 0);
				continue;
			}
		} else {
			/* Resolve the specified address */
			AF(&addr) = (u_short)my_node->addr->type;

			if (getnetnum(my_node->addr->address,
				      &addr, 1, t_UNK) != 1) {
				/*
				 * Attempt a blocking lookup.  This
				 * is in violation of the nonblocking
				 * design of ntpd's mainline code.  The
				 * alternative of running without the
				 * restriction until the name resolved
				 * seems worse.
				 * Ideally some scheme could be used for
				 * restrict directives in the startup
				 * ntp.conf to delay starting up the
				 * protocol machinery until after all
				 * restrict hosts have been resolved.
				 */
				ai_list = NULL;
				ZERO(hints);
				hints.ai_protocol = IPPROTO_UDP;
				hints.ai_socktype = SOCK_DGRAM;
				hints.ai_family = my_node->addr->type;
				rc = getaddrinfo(my_node->addr->address,
						 "ntp", &hints,
						 &ai_list);
				if (rc) {
					msyslog(LOG_ERR,
						"restrict: ignoring line %d, address/host '%s' unusable.",
						my_node->line_no,
						my_node->addr->address);
					continue;
				}
				INSIST(ai_list != NULL);
				pai = ai_list;
				INSIST(pai->ai_addr != NULL);
				INSIST(sizeof(addr) >=
					   pai->ai_addrlen);
				memcpy(&addr, pai->ai_addr,
				       pai->ai_addrlen);
				INSIST(AF_INET == AF(&addr) ||
					   AF_INET6 == AF(&addr));
			}

			SET_HOSTMASK(&mask, AF(&addr));

			/* Resolve the mask */
			if (my_node->mask) {
				ZERO_SOCK(&mask);
				AF(&mask) = my_node->mask->type;
				if (getnetnum(my_node->mask->address,
					      &mask, 1, t_MSK) != 1) {
					msyslog(LOG_ERR,
						"restrict: ignoring line %d, mask '%s' unusable.",
						my_node->line_no,
						my_node->mask->address);
					continue;
				}
			}
		}

		/* Set the flags */
		if (restrict_default) {
			AF(&addr) = AF_INET;
			AF(&mask) = AF_INET;
			hack_restrict(RESTRICT_FLAGS, &addr, &mask,
				      ippeerlimit, mflags, rflags, 0);
			AF(&addr) = AF_INET6;
			AF(&mask) = AF_INET6;
		}

		do {
			hack_restrict(RESTRICT_FLAGS, &addr, &mask,
				      ippeerlimit, mflags, rflags, 0);
			if (pai != NULL &&
			    NULL != (pai = pai->ai_next)) {
				INSIST(pai->ai_addr != NULL);
				INSIST(sizeof(addr) >=
					   pai->ai_addrlen);
				ZERO_SOCK(&addr);
				memcpy(&addr, pai->ai_addr,
				       pai->ai_addrlen);
				INSIST(AF_INET == AF(&addr) ||
					   AF_INET6 == AF(&addr));
				SET_HOSTMASK(&mask, AF(&addr));
			}
		} while (pai != NULL);

		if (ai_list != NULL)
			freeaddrinfo(ai_list);
	}
}
#endif	/* !SIM */


#ifdef FREE_CFG_T
static void
free_config_access(
	config_tree *ptree
	)
{
	FREE_ATTR_VAL_FIFO(ptree->mru_opts);
	FREE_ATTR_VAL_FIFO(ptree->discard_opts);
	FREE_RESTRICT_FIFO(ptree->restrict_opts);
}
#endif	/* FREE_CFG_T */


static void
config_rlimit(
	config_tree *ptree
	)
{
	attr_val *	rlimit_av;

	rlimit_av = HEAD_PFIFO(ptree->rlimit);
	for (; rlimit_av != NULL; rlimit_av = rlimit_av->link) {
		switch (rlimit_av->attr) {

		default:
			fatal_error("config-rlimit: value-token=%d", rlimit_av->attr);

		case T_Memlock:
			/* What if we HAVE_OPT(SAVECONFIGQUIT) ? */
			if (HAVE_OPT( SAVECONFIGQUIT )) {
				break;
			}
			if (rlimit_av->value.i == -1) {
# if defined(HAVE_MLOCKALL)
				if (cur_memlock != 0) {
					if (-1 == munlockall()) {
						msyslog(LOG_ERR, "munlockall() failed: %m");
					}
				}
				cur_memlock = 0;
# endif /* HAVE_MLOCKALL */
			} else if (rlimit_av->value.i >= 0) {
#if defined(RLIMIT_MEMLOCK)
# if defined(HAVE_MLOCKALL)
				if (cur_memlock != 1) {
					if (-1 == mlockall(MCL_CURRENT|MCL_FUTURE)) {
						msyslog(LOG_ERR, "mlockall() failed: %m");
					}
				}
# endif /* HAVE_MLOCKALL */
				ntp_rlimit(RLIMIT_MEMLOCK,
					   (rlim_t)(rlimit_av->value.i * 1024 * 1024),
					   1024 * 1024,
					   "MB");
				cur_memlock = 1;
#else
				/* STDERR as well would be fine... */
				msyslog(LOG_WARNING, "'rlimit memlock' specified but is not available on this system.");
#endif /* RLIMIT_MEMLOCK */
			} else {
				msyslog(LOG_WARNING, "'rlimit memlock' value of %d is unexpected!", rlimit_av->value.i);
			}
			break;

		case T_Stacksize:
#if defined(RLIMIT_STACK)
			ntp_rlimit(RLIMIT_STACK,
				   (rlim_t)(rlimit_av->value.i * 4096),
				   4096,
				   "4k");
#else
			/* STDERR as well would be fine... */
			msyslog(LOG_WARNING, "'rlimit stacksize' specified but is not available on this system.");
#endif /* RLIMIT_STACK */
			break;

		case T_Filenum:
#if defined(RLIMIT_NOFILE)
			ntp_rlimit(RLIMIT_NOFILE,
				  (rlim_t)(rlimit_av->value.i),
				  1,
				  "");
#else
			/* STDERR as well would be fine... */
			msyslog(LOG_WARNING, "'rlimit filenum' specified but is not available on this system.");
#endif /* RLIMIT_NOFILE */
			break;

		}
	}
}


static void
config_tinker(
	config_tree *ptree
	)
{
	attr_val *	tinker;
	int		item;

	tinker = HEAD_PFIFO(ptree->tinker);
	for (; tinker != NULL; tinker = tinker->link) {
		switch (tinker->attr) {

		default:
			fatal_error("config_tinker: attr-token=%d", tinker->attr);

		case T_Allan:
			item = LOOP_ALLAN;
			break;

		case T_Dispersion:
			item = LOOP_PHI;
			break;

		case T_Freq:
			item = LOOP_FREQ;
			break;

		case T_Huffpuff:
			item = LOOP_HUFFPUFF;
			break;

		case T_Panic:
			item = LOOP_PANIC;
			break;

		case T_Step:
			item = LOOP_MAX;
			break;

		case T_Stepback:
			item = LOOP_MAX_BACK;
			break;

		case T_Stepfwd:
			item = LOOP_MAX_FWD;
			break;

		case T_Stepout:
			item = LOOP_MINSTEP;
			break;

		case T_Tick:
			item = LOOP_TICK;
			break;
		}
		loop_config(item, tinker->value.d);
	}
}


#ifdef FREE_CFG_T
static void
free_config_rlimit(
	config_tree *ptree
	)
{
	FREE_ATTR_VAL_FIFO(ptree->rlimit);
}

static void
free_config_tinker(
	config_tree *ptree
	)
{
	FREE_ATTR_VAL_FIFO(ptree->tinker);
}
#endif	/* FREE_CFG_T */


/*
 * config_nic_rules - apply interface listen/ignore/drop items
 */
#ifndef SIM
static void
config_nic_rules(
	config_tree *ptree,
	int/*BOOL*/ input_from_file
	)
{
	nic_rule_node *	curr_node;
	sockaddr_u	addr;
	nic_rule_match	match_type;
	nic_rule_action	action;
	char *		if_name;
	char *		pchSlash;
	int		prefixlen;
	int		addrbits;

	curr_node = HEAD_PFIFO(ptree->nic_rules);

	if (curr_node != NULL
	    && (HAVE_OPT( NOVIRTUALIPS ) || HAVE_OPT( INTERFACE ))) {
		msyslog(LOG_ERR,
			"interface/nic rules are not allowed with --interface (-I) or --novirtualips (-L)%s",
			(input_from_file) ? ", exiting" : "");
		if (input_from_file)
			exit(1);
		else
			return;
	}

	for (; curr_node != NULL; curr_node = curr_node->link) {
		prefixlen = -1;
		if_name = curr_node->if_name;
		if (if_name != NULL)
			if_name = estrdup(if_name);

		switch (curr_node->match_class) {

		default:
			fatal_error("config_nic_rules: match-class-token=%d", curr_node->match_class);

		case 0:
			/*
			 * 0 is out of range for valid token T_...
			 * and in a nic_rules_node indicates the
			 * interface descriptor is either a name or
			 * address, stored in if_name in either case.
			 */
			INSIST(if_name != NULL);
			pchSlash = strchr(if_name, '/');
			if (pchSlash != NULL)
				*pchSlash = '\0';
			if (is_ip_address(if_name, AF_UNSPEC, &addr)) {
				match_type = MATCH_IFADDR;
				if (pchSlash != NULL
				    && 1 == sscanf(pchSlash + 1, "%d",
					    &prefixlen)) {
					addrbits = 8 *
					    SIZEOF_INADDR(AF(&addr));
					prefixlen = max(-1, prefixlen);
					prefixlen = min(prefixlen,
							addrbits);
				}
			} else {
				match_type = MATCH_IFNAME;
				if (pchSlash != NULL)
					*pchSlash = '/';
			}
			break;

		case T_All:
			match_type = MATCH_ALL;
			break;

		case T_Ipv4:
			match_type = MATCH_IPV4;
			break;

		case T_Ipv6:
			match_type = MATCH_IPV6;
			break;

		case T_Wildcard:
			match_type = MATCH_WILDCARD;
			break;
		}

		switch (curr_node->action) {

		default:
			fatal_error("config_nic_rules: action-token=%d", curr_node->action);

		case T_Listen:
			action = ACTION_LISTEN;
			break;

		case T_Ignore:
			action = ACTION_IGNORE;
			break;

		case T_Drop:
			action = ACTION_DROP;
			break;
		}

		add_nic_rule(match_type, if_name, prefixlen,
			     action);
		timer_interfacetimeout(current_time + 2);
		if (if_name != NULL)
			free(if_name);
	}
}
#endif	/* !SIM */


#ifdef FREE_CFG_T
static void
free_config_nic_rules(
	config_tree *ptree
	)
{
	nic_rule_node *curr_node;

	if (ptree->nic_rules != NULL) {
		for (;;) {
			UNLINK_FIFO(curr_node, *ptree->nic_rules, link);
			if (NULL == curr_node)
				break;
			free(curr_node->if_name);
			free(curr_node);
		}
		free(ptree->nic_rules);
		ptree->nic_rules = NULL;
	}
}
#endif	/* FREE_CFG_T */


static void
apply_enable_disable(
	attr_val_fifo *	fifo,
	int		enable
	)
{
	attr_val *curr_tok_fifo;
	int option;
#ifdef BC_LIST_FRAMEWORK_NOT_YET_USED
	bc_entry *pentry;
#endif

	for (curr_tok_fifo = HEAD_PFIFO(fifo);
	     curr_tok_fifo != NULL;
	     curr_tok_fifo = curr_tok_fifo->link) {

		option = curr_tok_fifo->value.i;
		switch (option) {

		default:
			msyslog(LOG_ERR,
				"can not apply enable/disable token %d, unknown",
				option);
			break;

		case T_Auth:
			proto_config(PROTO_AUTHENTICATE, enable, 0., NULL);
			break;

		case T_Bclient:
			proto_config(PROTO_BROADCLIENT, enable, 0., NULL);
			break;

		case T_Calibrate:
			proto_config(PROTO_CAL, enable, 0., NULL);
			break;

		case T_Kernel:
			proto_config(PROTO_KERNEL, enable, 0., NULL);
			break;

		case T_Monitor:
			proto_config(PROTO_MONITOR, enable, 0., NULL);
			break;

		case T_Mode7:
			proto_config(PROTO_MODE7, enable, 0., NULL);
			break;

		case T_Ntp:
			proto_config(PROTO_NTP, enable, 0., NULL);
			break;

		case T_PCEdigest:
			proto_config(PROTO_PCEDIGEST, enable, 0., NULL);
			break;

		case T_Stats:
			proto_config(PROTO_FILEGEN, enable, 0., NULL);
			break;

		case T_UEcrypto:
			proto_config(PROTO_UECRYPTO, enable, 0., NULL);
			break;

		case T_UEcryptonak:
			proto_config(PROTO_UECRYPTONAK, enable, 0., NULL);
			break;

		case T_UEdigest:
			proto_config(PROTO_UEDIGEST, enable, 0., NULL);
			break;

#ifdef BC_LIST_FRAMEWORK_NOT_YET_USED
		case T_Bc_bugXXXX:
			pentry = bc_list;
			while (pentry->token) {
				if (pentry->token == option)
					break;
				pentry++;
			}
			if (!pentry->token) {
				msyslog(LOG_ERR,
					"compat token %d not in bc_list[]",
					option);
				continue;
			}
			pentry->enabled = enable;
			break;
#endif
		}
	}
}


static void
config_system_opts(
	config_tree *ptree
	)
{
	apply_enable_disable(ptree->enable_opts, 1);
	apply_enable_disable(ptree->disable_opts, 0);
}


#ifdef FREE_CFG_T
static void
free_config_system_opts(
	config_tree *ptree
	)
{
	FREE_ATTR_VAL_FIFO(ptree->enable_opts);
	FREE_ATTR_VAL_FIFO(ptree->disable_opts);
}
#endif	/* FREE_CFG_T */


static void
config_logconfig(
	config_tree *ptree
	)
{
	attr_val *	my_lc;

	my_lc = HEAD_PFIFO(ptree->logconfig);
	for (; my_lc != NULL; my_lc = my_lc->link) {
		switch (my_lc->attr) {

		case '+':
			ntp_syslogmask |= get_logmask(my_lc->value.s);
			break;

		case '-':
			ntp_syslogmask &= ~get_logmask(my_lc->value.s);
			break;

		case '=':
			ntp_syslogmask = get_logmask(my_lc->value.s);
			break;
		default:
			fatal_error("config-logconfig: modifier='%c'", my_lc->attr);
		}
	}
}


#ifdef FREE_CFG_T
static void
free_config_logconfig(
	config_tree *ptree
	)
{
	FREE_ATTR_VAL_FIFO(ptree->logconfig);
}
#endif	/* FREE_CFG_T */


#ifndef SIM
static void
config_phone(
	config_tree *ptree
	)
{
	size_t		i;
	string_node *	sn;

	i = 0;
	sn = HEAD_PFIFO(ptree->phone);
	for (; sn != NULL; sn = sn->link) {
		/* need to leave array entry for NULL terminator */
		if (i < COUNTOF(sys_phone) - 1) {
			sys_phone[i++] = estrdup(sn->s);
			sys_phone[i] = NULL;
		} else {
			msyslog(LOG_INFO,
				"phone: Number of phone entries exceeds %zu. Ignoring phone %s...",
				(COUNTOF(sys_phone) - 1), sn->s);
		}
	}
}
#endif	/* !SIM */

static void
config_mdnstries(
	config_tree *ptree
	)
{
#ifdef HAVE_DNSREGISTRATION
	extern int mdnstries;
	mdnstries = ptree->mdnstries;
#endif  /* HAVE_DNSREGISTRATION */
}

#ifdef FREE_CFG_T
static void
free_config_phone(
	config_tree *ptree
	)
{
	FREE_STRING_FIFO(ptree->phone);
}
#endif	/* FREE_CFG_T */


#ifndef SIM
static void
config_setvar(
	config_tree *ptree
	)
{
	setvar_node *my_node;
	size_t	varlen, vallen, octets;
	char *	str;

	str = NULL;
	my_node = HEAD_PFIFO(ptree->setvar);
	for (; my_node != NULL; my_node = my_node->link) {
		varlen = strlen(my_node->var);
		vallen = strlen(my_node->val);
		octets = varlen + vallen + 1 + 1;
		str = erealloc(str, octets);
		snprintf(str, octets, "%s=%s", my_node->var,
			 my_node->val);
		set_sys_var(str, octets, (my_node->isdefault)
						? DEF
						: 0);
	}
	if (str != NULL)
		free(str);
}
#endif	/* !SIM */


#ifdef FREE_CFG_T
static void
free_config_setvar(
	config_tree *ptree
	)
{
	FREE_SETVAR_FIFO(ptree->setvar);
}
#endif	/* FREE_CFG_T */


#ifndef SIM
static void
config_ttl(
	config_tree *ptree
	)
{
	size_t i = 0;
	int_node *curr_ttl;

	/* [Bug 3465] There is a built-in default for the TTLs. We must
	 * overwrite 'sys_ttlmax' if we change that preset, and leave it
	 * alone otherwise!
	 */
	curr_ttl = HEAD_PFIFO(ptree->ttl);
	for (; curr_ttl != NULL; curr_ttl = curr_ttl->link) {
		if (i < COUNTOF(sys_ttl))
			sys_ttl[i++] = (u_char)curr_ttl->i;
		else
			msyslog(LOG_INFO,
				"ttl: Number of TTL entries exceeds %zu. Ignoring TTL %d...",
				COUNTOF(sys_ttl), curr_ttl->i);
	}
	if (0 != i) /* anything written back at all? */
		sys_ttlmax = i - 1;
}
#endif	/* !SIM */


#ifdef FREE_CFG_T
static void
free_config_ttl(
	config_tree *ptree
	)
{
	FREE_INT_FIFO(ptree->ttl);
}
#endif	/* FREE_CFG_T */


#ifndef SIM
static void
config_trap(
	config_tree *ptree
	)
{
	addr_opts_node *curr_trap;
	attr_val *curr_opt;
	sockaddr_u addr_sock;
	sockaddr_u peeraddr;
	struct interface *localaddr;
	struct addrinfo hints;
	char port_text[8];
	settrap_parms *pstp;
	u_short port;
	int err_flag;
	int rc;

	/* silence warning about addr_sock potentially uninitialized */
	AF(&addr_sock) = AF_UNSPEC;

	curr_trap = HEAD_PFIFO(ptree->trap);
	for (; curr_trap != NULL; curr_trap = curr_trap->link) {
		err_flag = 0;
		port = 0;
		localaddr = NULL;

		curr_opt = HEAD_PFIFO(curr_trap->options);
		for (; curr_opt != NULL; curr_opt = curr_opt->link) {
			if (T_Port == curr_opt->attr) {
				if (curr_opt->value.i < 1
				    || curr_opt->value.i > USHRT_MAX) {
					msyslog(LOG_ERR,
						"invalid port number "
						"%d, trap ignored",
						curr_opt->value.i);
					err_flag = 1;
				}
				port = (u_short)curr_opt->value.i;
			}
			else if (T_Interface == curr_opt->attr) {
				/* Resolve the interface address */
				ZERO_SOCK(&addr_sock);
				if (getnetnum(curr_opt->value.s,
					      &addr_sock, 1, t_UNK) != 1) {
					err_flag = 1;
					break;
				}

				localaddr = findinterface(&addr_sock);

				if (NULL == localaddr) {
					msyslog(LOG_ERR,
						"can't find interface with address %s",
						stoa(&addr_sock));
					err_flag = 1;
				}
			}
		}

		/* Now process the trap for the specified interface
		 * and port number
		 */
		if (!err_flag) {
			if (!port)
				port = TRAPPORT;
			ZERO_SOCK(&peeraddr);
			rc = getnetnum(curr_trap->addr->address,
				       &peeraddr, 1, t_UNK);
			if (1 != rc) {
#ifndef WORKER
				msyslog(LOG_ERR,
					"trap: unable to use IP address %s.",
					curr_trap->addr->address);
#else	/* WORKER follows */
				/*
				 * save context and hand it off
				 * for name resolution.
				 */
				ZERO(hints);
				hints.ai_protocol = IPPROTO_UDP;
				hints.ai_socktype = SOCK_DGRAM;
				snprintf(port_text, sizeof(port_text),
					 "%u", port);
				hints.ai_flags = Z_AI_NUMERICSERV;
				pstp = emalloc_zero(sizeof(*pstp));
				if (localaddr != NULL) {
					hints.ai_family = localaddr->family;
					pstp->ifaddr_nonnull = 1;
					memcpy(&pstp->ifaddr,
					       &localaddr->sin,
					       sizeof(pstp->ifaddr));
				}
				rc = getaddrinfo_sometime(
					curr_trap->addr->address,
					port_text, &hints,
					INITIAL_DNS_RETRY,
					&trap_name_resolved,
					pstp);
				if (!rc)
					msyslog(LOG_ERR,
						"config_trap: getaddrinfo_sometime(%s,%s): %m",
						curr_trap->addr->address,
						port_text);
#endif	/* WORKER */
				continue;
			}
			/* port is at same location for v4 and v6 */
			SET_PORT(&peeraddr, port);

			if (NULL == localaddr)
				localaddr = ANY_INTERFACE_CHOOSE(&peeraddr);
			else
				AF(&peeraddr) = AF(&addr_sock);

			if (!ctlsettrap(&peeraddr, localaddr, 0,
					NTP_VERSION))
				msyslog(LOG_ERR,
					"set trap %s -> %s failed.",
					latoa(localaddr),
					stoa(&peeraddr));
		}
	}
}


/*
 * trap_name_resolved()
 *
 * Callback invoked when config_trap()'s DNS lookup completes.
 */
# ifdef WORKER
static void
trap_name_resolved(
	int			rescode,
	int			gai_errno,
	void *			context,
	const char *		name,
	const char *		service,
	const struct addrinfo *	hints,
	const struct addrinfo *	res
	)
{
	settrap_parms *pstp;
	struct interface *localaddr;
	sockaddr_u peeraddr;

	(void)gai_errno;
	(void)service;
	(void)hints;
	pstp = context;
	if (rescode) {
		msyslog(LOG_ERR,
			"giving up resolving trap host %s: %s (%d)",
			name, gai_strerror(rescode), rescode);
		free(pstp);
		return;
	}
	INSIST(sizeof(peeraddr) >= res->ai_addrlen);
	ZERO(peeraddr);
	memcpy(&peeraddr, res->ai_addr, res->ai_addrlen);
	localaddr = NULL;
	if (pstp->ifaddr_nonnull)
		localaddr = findinterface(&pstp->ifaddr);
	if (NULL == localaddr)
		localaddr = ANY_INTERFACE_CHOOSE(&peeraddr);
	if (!ctlsettrap(&peeraddr, localaddr, 0, NTP_VERSION))
		msyslog(LOG_ERR, "set trap %s -> %s failed.",
			latoa(localaddr), stoa(&peeraddr));
	free(pstp);
}
# endif	/* WORKER */
#endif	/* !SIM */


#ifdef FREE_CFG_T
static void
free_config_trap(
	config_tree *ptree
	)
{
	FREE_ADDR_OPTS_FIFO(ptree->trap);
}
#endif	/* FREE_CFG_T */


#ifndef SIM
static void
config_fudge(
	config_tree *ptree
	)
{
	addr_opts_node *curr_fudge;
	attr_val *curr_opt;
	sockaddr_u addr_sock;
	address_node *addr_node;
	struct refclockstat clock_stat;
	int err_flag;

	curr_fudge = HEAD_PFIFO(ptree->fudge);
	for (; curr_fudge != NULL; curr_fudge = curr_fudge->link) {
		err_flag = 0;

		/* Get the reference clock address and
		 * ensure that it is sane
		 */
		addr_node = curr_fudge->addr;
		ZERO_SOCK(&addr_sock);
		if (getnetnum(addr_node->address, &addr_sock, 1, t_REF)
		    != 1) {
			err_flag = 1;
			msyslog(LOG_ERR,
				"unrecognized fudge reference clock address %s, line ignored",
				addr_node->address);
		} else if (!ISREFCLOCKADR(&addr_sock)) {
			err_flag = 1;
			msyslog(LOG_ERR,
				"inappropriate address %s for the fudge command, line ignored",
				stoa(&addr_sock));
		}

		/* Parse all the options to the fudge command */
		ZERO(clock_stat);
		curr_opt = HEAD_PFIFO(curr_fudge->options);
		for (; curr_opt != NULL; curr_opt = curr_opt->link) {
			switch (curr_opt->attr) {

			case T_Time1:
				clock_stat.haveflags |= CLK_HAVETIME1;
				clock_stat.fudgetime1 = curr_opt->value.d;
				break;

			case T_Time2:
				clock_stat.haveflags |= CLK_HAVETIME2;
				clock_stat.fudgetime2 = curr_opt->value.d;
				break;

			case T_Stratum:
				clock_stat.haveflags |= CLK_HAVEVAL1;
				clock_stat.fudgeval1 = curr_opt->value.i;
				break;

			case T_Refid:
				clock_stat.haveflags |= CLK_HAVEVAL2;
				clock_stat.fudgeval2 = 0;
				memcpy(&clock_stat.fudgeval2,
				       curr_opt->value.s,
				       min(strlen(curr_opt->value.s), 4));
				break;

			case T_Flag1:
				clock_stat.haveflags |= CLK_HAVEFLAG1;
				if (curr_opt->value.i)
					clock_stat.flags |= CLK_FLAG1;
				else
					clock_stat.flags &= ~CLK_FLAG1;
				break;

			case T_Flag2:
				clock_stat.haveflags |= CLK_HAVEFLAG2;
				if (curr_opt->value.i)
					clock_stat.flags |= CLK_FLAG2;
				else
					clock_stat.flags &= ~CLK_FLAG2;
				break;

			case T_Flag3:
				clock_stat.haveflags |= CLK_HAVEFLAG3;
				if (curr_opt->value.i)
					clock_stat.flags |= CLK_FLAG3;
				else
					clock_stat.flags &= ~CLK_FLAG3;
				break;

			case T_Flag4:
				clock_stat.haveflags |= CLK_HAVEFLAG4;
				if (curr_opt->value.i)
					clock_stat.flags |= CLK_FLAG4;
				else
					clock_stat.flags &= ~CLK_FLAG4;
				break;

			default:
				msyslog(LOG_ERR,
					"Unexpected fudge flag %s (%d) for %s",
					token_name(curr_opt->attr),
					curr_opt->attr, addr_node->address);
				exit(curr_opt->attr ? curr_opt->attr : 1);
			}
		}
# ifdef REFCLOCK
		if (!err_flag)
			refclock_control(&addr_sock, &clock_stat, NULL);
# endif
	}
}
#endif	/* !SIM */


#ifdef FREE_CFG_T
static void
free_config_fudge(
	config_tree *ptree
	)
{
	FREE_ADDR_OPTS_FIFO(ptree->fudge);
}
#endif	/* FREE_CFG_T */


static void
config_vars(
	config_tree *ptree
	)
{
	attr_val *curr_var;
	int len;

	curr_var = HEAD_PFIFO(ptree->vars);
	for (; curr_var != NULL; curr_var = curr_var->link) {
		/* Determine which variable to set and set it */
		switch (curr_var->attr) {

		case T_Broadcastdelay:
			proto_config(PROTO_BROADDELAY, 0, curr_var->value.d, NULL);
			break;

		case T_Tick:
			loop_config(LOOP_TICK, curr_var->value.d);
			break;

		case T_Driftfile:
			if ('\0' == curr_var->value.s[0]) {
				stats_drift_file = 0;
				msyslog(LOG_INFO, "config: driftfile disabled");
			} else
				stats_config(STATS_FREQ_FILE, curr_var->value.s);
			break;

		case T_Dscp:
			/* DSCP is in the upper 6 bits of the IP TOS/DS field */
			qos = curr_var->value.i << 2;
			break;

		case T_Ident:
			sys_ident = curr_var->value.s;
			break;

		case T_WanderThreshold:		/* FALLTHROUGH */
		case T_Nonvolatile:
			wander_threshold = curr_var->value.d;
			break;

		case T_Leapfile:
			stats_config(STATS_LEAP_FILE, curr_var->value.s);
			break;

#ifdef LEAP_SMEAR
		case T_Leapsmearinterval:
			leap_smear_intv = curr_var->value.i;
			msyslog(LOG_INFO, "config: leap smear interval %i s", leap_smear_intv);
			break;
#endif

		case T_Pidfile:
			stats_config(STATS_PID_FILE, curr_var->value.s);
			break;

		case T_Logfile:
			if (-1 == change_logfile(curr_var->value.s, TRUE))
				msyslog(LOG_ERR,
					"Cannot open logfile %s: %m",
					curr_var->value.s);
			break;

		case T_Saveconfigdir:
			if (saveconfigdir != NULL)
				free(saveconfigdir);
			len = strlen(curr_var->value.s);
			if (0 == len) {
				saveconfigdir = NULL;
			} else if (DIR_SEP != curr_var->value.s[len - 1]
#ifdef SYS_WINNT	/* slash is also a dir. sep. on Windows */
				   && '/' != curr_var->value.s[len - 1]
#endif
				 ) {
					len++;
					saveconfigdir = emalloc(len + 1);
					snprintf(saveconfigdir, len + 1,
						 "%s%c",
						 curr_var->value.s,
						 DIR_SEP);
			} else {
					saveconfigdir = estrdup(
					    curr_var->value.s);
			}
			break;

		case T_Automax:
#ifdef AUTOKEY
			if (curr_var->value.i > 2 && curr_var->value.i < 32)
				sys_automax = (u_char)curr_var->value.i;
			else
				msyslog(LOG_ERR,
					"'automax' value %d ignored",
					curr_var->value.i);
#endif
			break;

		default:
			msyslog(LOG_ERR,
				"config_vars(): unexpected token %d",
				curr_var->attr);
		}
	}
}


#ifdef FREE_CFG_T
static void
free_config_vars(
	config_tree *ptree
	)
{
	FREE_ATTR_VAL_FIFO(ptree->vars);
}
#endif	/* FREE_CFG_T */


/* Define a function to check if a resolved address is sane.
 * If yes, return 1, else return 0;
 */
static int
is_sane_resolved_address(
	sockaddr_u *	peeraddr,
	int		hmode
	)
{
	if (!ISREFCLOCKADR(peeraddr) && ISBADADR(peeraddr)) {
		msyslog(LOG_ERR,
			"attempt to configure invalid address %s",
			stoa(peeraddr));
		return 0;
	}
	/*
	 * Shouldn't be able to specify multicast
	 * address for server/peer!
	 * and unicast address for manycastclient!
	 */
	if ((T_Server == hmode || T_Peer == hmode || T_Pool == hmode)
	    && IS_MCAST(peeraddr)) {
		msyslog(LOG_ERR,
			"attempt to configure invalid address %s",
			stoa(peeraddr));
		return 0;
	}
	if (T_Manycastclient == hmode && !IS_MCAST(peeraddr)) {
		msyslog(LOG_ERR,
			"attempt to configure invalid address %s",
			stoa(peeraddr));
		return 0;
	}

	if (IS_IPV6(peeraddr) && !ipv6_works)
		return 0;

	/* Ok, all tests succeeded, now we can return 1 */
	return 1;
}


#ifndef SIM
static u_char
get_correct_host_mode(
	int token
	)
{
	switch (token) {

	case T_Server:
	case T_Pool:
	case T_Manycastclient:
		return MODE_CLIENT;

	case T_Peer:
		return MODE_ACTIVE;

	case T_Broadcast:
		return MODE_BROADCAST;

	default:
		return 0;
	}
}


/*
 * peerflag_bits()	get config_peers() peerflags value from a
 *			peer_node's queue of flag attr_val entries.
 */
static int
peerflag_bits(
	peer_node *pn
	)
{
	int peerflags;
	attr_val *option;

	/* translate peerflags options to bits */
	peerflags = 0;
	option = HEAD_PFIFO(pn->peerflags);
	for (; option != NULL; option = option->link) {
		switch (option->value.i) {

		default:
			fatal_error("peerflag_bits: option-token=%d", option->value.i);

		case T_Autokey:
			peerflags |= FLAG_SKEY;
			break;

		case T_Burst:
			peerflags |= FLAG_BURST;
			break;

		case T_Iburst:
			peerflags |= FLAG_IBURST;
			break;

		case T_Noselect:
			peerflags |= FLAG_NOSELECT;
			break;

		case T_Preempt:
			peerflags |= FLAG_PREEMPT;
			break;

		case T_Prefer:
			peerflags |= FLAG_PREFER;
			break;

		case T_True:
			peerflags |= FLAG_TRUE;
			break;

		case T_Xleave:
			peerflags |= FLAG_XLEAVE;
			break;
		}
	}

	return peerflags;
}


static void
config_peers(
	config_tree *ptree
	)
{
	sockaddr_u		peeraddr;
	struct addrinfo		hints;
	peer_node *		curr_peer;
	peer_resolved_ctx *	ctx;
	u_char			hmode;

	/* add servers named on the command line with iburst implied */
	for (;
	     cmdline_server_count > 0;
	     cmdline_server_count--, cmdline_servers++) {

		ZERO_SOCK(&peeraddr);
		/*
		 * If we have a numeric address, we can safely
		 * proceed in the mainline with it.  Otherwise, hand
		 * the hostname off to the blocking child.
		 *
		 * Note that if we're told to add the peer here, we
		 * do that regardless of ippeerlimit.
		 */
		if (is_ip_address(*cmdline_servers, AF_UNSPEC,
				  &peeraddr)) {

			SET_PORT(&peeraddr, NTP_PORT);
			if (is_sane_resolved_address(&peeraddr,
						     T_Server))
				peer_config(
					&peeraddr,
					NULL,
					NULL,
					-1,
					MODE_CLIENT,
					NTP_VERSION,
					0,
					0,
					FLAG_IBURST,
					0,
					0,
					NULL);
		} else {
			/* we have a hostname to resolve */
# ifdef WORKER
			ctx = emalloc_zero(sizeof(*ctx));
			ctx->family = AF_UNSPEC;
			ctx->host_mode = T_Server;
			ctx->hmode = MODE_CLIENT;
			ctx->version = NTP_VERSION;
			ctx->flags = FLAG_IBURST;

			ZERO(hints);
			hints.ai_family = (u_short)ctx->family;
			hints.ai_socktype = SOCK_DGRAM;
			hints.ai_protocol = IPPROTO_UDP;

			getaddrinfo_sometime_ex(*cmdline_servers,
					     "ntp", &hints,
					     INITIAL_DNS_RETRY,
					     &peer_name_resolved,
					     (void *)ctx, DNSFLAGS);
# else	/* !WORKER follows */
			msyslog(LOG_ERR,
				"hostname %s can not be used, please use IP address instead.",
				curr_peer->addr->address);
# endif
		}
	}

	/* add associations from the configuration file */
	curr_peer = HEAD_PFIFO(ptree->peers);
	for (; curr_peer != NULL; curr_peer = curr_peer->link) {
		ZERO_SOCK(&peeraddr);
		/* Find the correct host-mode */
		hmode = get_correct_host_mode(curr_peer->host_mode);
		INSIST(hmode != 0);

		if (T_Pool == curr_peer->host_mode) {
			AF(&peeraddr) = curr_peer->addr->type;
			peer_config(
				&peeraddr,
				curr_peer->addr->address,
				NULL,
				-1,
				hmode,
				curr_peer->peerversion,
				curr_peer->minpoll,
				curr_peer->maxpoll,
				peerflag_bits(curr_peer),
				curr_peer->ttl,
				curr_peer->peerkey,
				curr_peer->group);
		/*
		 * If we have a numeric address, we can safely
		 * proceed in the mainline with it.  Otherwise, hand
		 * the hostname off to the blocking child.
		 */
		} else if (is_ip_address(curr_peer->addr->address,
				  curr_peer->addr->type, &peeraddr)) {

			SET_PORT(&peeraddr, NTP_PORT);
			if (is_sane_resolved_address(&peeraddr,
			    curr_peer->host_mode))
				peer_config(
					&peeraddr,
					NULL,
					NULL,
					-1,
					hmode,
					curr_peer->peerversion,
					curr_peer->minpoll,
					curr_peer->maxpoll,
					peerflag_bits(curr_peer),
					curr_peer->ttl,
					curr_peer->peerkey,
					curr_peer->group);
		} else {
			/* we have a hostname to resolve */
# ifdef WORKER
			ctx = emalloc_zero(sizeof(*ctx));
			ctx->family = curr_peer->addr->type;
			ctx->host_mode = curr_peer->host_mode;
			ctx->hmode = hmode;
			ctx->version = curr_peer->peerversion;
			ctx->minpoll = curr_peer->minpoll;
			ctx->maxpoll = curr_peer->maxpoll;
			ctx->flags = peerflag_bits(curr_peer);
			ctx->ttl = curr_peer->ttl;
			ctx->keyid = curr_peer->peerkey;
			ctx->group = curr_peer->group;

			ZERO(hints);
			hints.ai_family = ctx->family;
			hints.ai_socktype = SOCK_DGRAM;
			hints.ai_protocol = IPPROTO_UDP;

			getaddrinfo_sometime_ex(curr_peer->addr->address,
					     "ntp", &hints,
					     INITIAL_DNS_RETRY,
					     &peer_name_resolved, ctx,
					     DNSFLAGS);
# else	/* !WORKER follows */
			msyslog(LOG_ERR,
				"hostname %s can not be used, please use IP address instead.",
				curr_peer->addr->address);
# endif
		}
	}
}
#endif	/* !SIM */

/*
 * peer_name_resolved()
 *
 * Callback invoked when config_peers()'s DNS lookup completes.
 */
#ifdef WORKER
static void
peer_name_resolved(
	int			rescode,
	int			gai_errno,
	void *			context,
	const char *		name,
	const char *		service,
	const struct addrinfo *	hints,
	const struct addrinfo *	res
	)
{
	sockaddr_u		peeraddr;
	peer_resolved_ctx *	ctx;
	u_short			af;
	const char *		fam_spec;

	(void)gai_errno;
	(void)service;
	(void)hints;
	ctx = context;

	DPRINTF(1, ("peer_name_resolved(%s) rescode %d\n", name, rescode));

	if (rescode) {
		free(ctx);
		msyslog(LOG_ERR,
			"giving up resolving host %s: %s (%d)",
			name, gai_strerror(rescode), rescode);
		return;
	}

	/* Loop to configure a single association */
	for (; res != NULL; res = res->ai_next) {
		memcpy(&peeraddr, res->ai_addr, res->ai_addrlen);
		if (is_sane_resolved_address(&peeraddr,
					     ctx->host_mode)) {
			NLOG(NLOG_SYSINFO) {
				af = ctx->family;
				fam_spec = (AF_INET6 == af)
					       ? "(AAAA) "
					       : (AF_INET == af)
						     ? "(A) "
						     : "";
				msyslog(LOG_INFO, "DNS %s %s-> %s",
					name, fam_spec,
					stoa(&peeraddr));
			}
			peer_config(
				&peeraddr,
				NULL,
				NULL,
				-1,
				ctx->hmode,
				ctx->version,
				ctx->minpoll,
				ctx->maxpoll,
				ctx->flags,
				ctx->ttl,
				ctx->keyid,
				ctx->group);
			break;
		}
	}
	free(ctx);
}
#endif	/* WORKER */


#ifdef FREE_CFG_T
static void
free_config_peers(
	config_tree *ptree
	)
{
	peer_node *curr_peer;

	if (ptree->peers != NULL) {
		for (;;) {
			UNLINK_FIFO(curr_peer, *ptree->peers, link);
			if (NULL == curr_peer)
				break;
			destroy_address_node(curr_peer->addr);
			destroy_attr_val_fifo(curr_peer->peerflags);
			free(curr_peer);
		}
		free(ptree->peers);
		ptree->peers = NULL;
	}
}
#endif	/* FREE_CFG_T */


#ifndef SIM
static void
config_unpeers(
	config_tree *ptree
	)
{
	sockaddr_u		peeraddr;
	struct addrinfo		hints;
	unpeer_node *		curr_unpeer;
	struct peer *		p;
	const char *		name;
	int			rc;

	curr_unpeer = HEAD_PFIFO(ptree->unpeers);
	for (; curr_unpeer != NULL; curr_unpeer = curr_unpeer->link) {
		/*
		 * If we have no address attached, assume we have to
		 * unpeer by AssocID.
		 */
		if (!curr_unpeer->addr) {
			p = findpeerbyassoc(curr_unpeer->assocID);
			if (p != NULL) {
				msyslog(LOG_NOTICE, "unpeered %s",
					stoa(&p->srcadr));
				peer_clear(p, "GONE");
				unpeer(p);
			}
			continue;
		}

		ZERO(peeraddr);
		AF(&peeraddr) = curr_unpeer->addr->type;
		name = curr_unpeer->addr->address;
		rc = getnetnum(name, &peeraddr, 0, t_UNK);
		/* Do we have a numeric address? */
		if (rc > 0) {
			DPRINTF(1, ("unpeer: searching for %s\n",
				    stoa(&peeraddr)));
			p = findexistingpeer(&peeraddr, NULL, NULL, -1, 0, NULL);
			if (p != NULL) {
				msyslog(LOG_NOTICE, "unpeered %s",
					stoa(&peeraddr));
				peer_clear(p, "GONE");
				unpeer(p);
			}
			continue;
		}
		/*
		 * It's not a numeric IP address, it's a hostname.
		 * Check for associations with a matching hostname.
		 */
		for (p = peer_list; p != NULL; p = p->p_link)
			if (p->hostname != NULL)
				if (!strcasecmp(p->hostname, name))
					break;
		if (p != NULL) {
			msyslog(LOG_NOTICE, "unpeered %s", name);
			peer_clear(p, "GONE");
			unpeer(p);
		}
		/* Resolve the hostname to address(es). */
# ifdef WORKER
		ZERO(hints);
		hints.ai_family = curr_unpeer->addr->type;
		hints.ai_socktype = SOCK_DGRAM;
		hints.ai_protocol = IPPROTO_UDP;
		getaddrinfo_sometime(name, "ntp", &hints,
				     INITIAL_DNS_RETRY,
				     &unpeer_name_resolved, NULL);
# else	/* !WORKER follows */
		msyslog(LOG_ERR,
			"hostname %s can not be used, please use IP address instead.",
			name);
# endif
	}
}
#endif	/* !SIM */


/*
 * unpeer_name_resolved()
 *
 * Callback invoked when config_unpeers()'s DNS lookup completes.
 */
#ifdef WORKER
static void
unpeer_name_resolved(
	int			rescode,
	int			gai_errno,
	void *			context,
	const char *		name,
	const char *		service,
	const struct addrinfo *	hints,
	const struct addrinfo *	res
	)
{
	sockaddr_u	peeraddr;
	struct peer *	peer;
	u_short		af;
	const char *	fam_spec;

	(void)context;
	(void)hints;
	DPRINTF(1, ("unpeer_name_resolved(%s) rescode %d\n", name, rescode));

	if (rescode) {
		msyslog(LOG_ERR, "giving up resolving unpeer %s: %s (%d)",
			name, gai_strerror(rescode), rescode);
		return;
	}
	/*
	 * Loop through the addresses found
	 */
	for (; res != NULL; res = res->ai_next) {
		INSIST(res->ai_addrlen <= sizeof(peeraddr));
		memcpy(&peeraddr, res->ai_addr, res->ai_addrlen);
		DPRINTF(1, ("unpeer: searching for peer %s\n",
			    stoa(&peeraddr)));
		peer = findexistingpeer(&peeraddr, NULL, NULL, -1, 0, NULL);
		if (peer != NULL) {
			af = AF(&peeraddr);
			fam_spec = (AF_INET6 == af)
				       ? "(AAAA) "
				       : (AF_INET == af)
					     ? "(A) "
					     : "";
			msyslog(LOG_NOTICE, "unpeered %s %s-> %s", name,
				fam_spec, stoa(&peeraddr));
			peer_clear(peer, "GONE");
			unpeer(peer);
		}
	}
}
#endif	/* WORKER */


#ifdef FREE_CFG_T
static void
free_config_unpeers(
	config_tree *ptree
	)
{
	unpeer_node *curr_unpeer;

	if (ptree->unpeers != NULL) {
		for (;;) {
			UNLINK_FIFO(curr_unpeer, *ptree->unpeers, link);
			if (NULL == curr_unpeer)
				break;
			destroy_address_node(curr_unpeer->addr);
			free(curr_unpeer);
		}
		free(ptree->unpeers);
	}
}
#endif	/* FREE_CFG_T */


#ifndef SIM
static void
config_reset_counters(
	config_tree *ptree
	)
{
	int_node *counter_set;

	for (counter_set = HEAD_PFIFO(ptree->reset_counters);
	     counter_set != NULL;
	     counter_set = counter_set->link) {
		switch (counter_set->i) {
		default:
			DPRINTF(1, ("config_reset_counters %s (%d) invalid\n",
				    keyword(counter_set->i), counter_set->i));
			break;

		case T_Allpeers:
			peer_all_reset();
			break;

		case T_Auth:
			reset_auth_stats();
			break;

		case T_Ctl:
			ctl_clr_stats();
			break;

		case T_Io:
			io_clr_stats();
			break;

		case T_Mem:
			peer_clr_stats();
			break;

		case T_Sys:
			proto_clr_stats();
			break;

		case T_Timer:
			timer_clr_stats();
			break;
		}
	}
}
#endif	/* !SIM */


#ifdef FREE_CFG_T
static void
free_config_reset_counters(
	config_tree *ptree
	)
{
	FREE_INT_FIFO(ptree->reset_counters);
}
#endif	/* FREE_CFG_T */


#ifdef SIM
static void
config_sim(
	config_tree *ptree
	)
{
	int i;
	server_info *serv_info;
	attr_val *init_stmt;
	sim_node *sim_n;

	/* Check if a simulate block was found in the configuration code.
	 * If not, return an error and exit
	 */
	sim_n = HEAD_PFIFO(ptree->sim_details);
	if (NULL == sim_n) {
		fprintf(stderr, "ERROR!! I couldn't find a \"simulate\" block for configuring the simulator.\n");
		fprintf(stderr, "\tCheck your configuration file.\n");
		exit(1);
	}

	/* Process the initialization statements
	 * -------------------------------------
	 */
	init_stmt = HEAD_PFIFO(sim_n->init_opts);
	for (; init_stmt != NULL; init_stmt = init_stmt->link) {
		switch(init_stmt->attr) {

		case T_Beep_Delay:
			simulation.beep_delay = init_stmt->value.d;
			break;

		case T_Sim_Duration:
			simulation.end_time = init_stmt->value.d;
			break;

		default:
			fprintf(stderr,
				"Unknown simulator init token %d\n",
				init_stmt->attr);
			exit(1);
		}
	}

	/* Process the server list
	 * -----------------------
	 */
	simulation.num_of_servers = 0;
	serv_info = HEAD_PFIFO(sim_n->servers);
	for (; serv_info != NULL; serv_info = serv_info->link)
		simulation.num_of_servers++;
	simulation.servers = eallocarray(simulation.num_of_servers,
				     sizeof(simulation.servers[0]));

	i = 0;
	serv_info = HEAD_PFIFO(sim_n->servers);
	for (; serv_info != NULL; serv_info = serv_info->link) {
		if (NULL == serv_info) {
			fprintf(stderr, "Simulator server list is corrupt\n");
			exit(1);
		} else {
			simulation.servers[i] = *serv_info;
			simulation.servers[i].link = NULL;
			i++;
		}
	}

	printf("Creating server associations\n");
	create_server_associations();
	fprintf(stderr,"\tServer associations successfully created!!\n");
}


#ifdef FREE_CFG_T
static void
free_config_sim(
	config_tree *ptree
	)
{
	sim_node *sim_n;
	server_info *serv_n;
	script_info *script_n;

	if (NULL == ptree->sim_details)
		return;
	sim_n = HEAD_PFIFO(ptree->sim_details);
	free(ptree->sim_details);
	ptree->sim_details = NULL;
	if (NULL == sim_n)
		return;

	FREE_ATTR_VAL_FIFO(sim_n->init_opts);
	for (;;) {
		UNLINK_FIFO(serv_n, *sim_n->servers, link);
		if (NULL == serv_n)
			break;
		free(serv_n->curr_script);
		if (serv_n->script != NULL) {
			for (;;) {
				UNLINK_FIFO(script_n, *serv_n->script,
					    link);
				if (script_n == NULL)
					break;
				free(script_n);
			}
			free(serv_n->script);
		}
		free(serv_n);
	}
	free(sim_n);
}
#endif	/* FREE_CFG_T */
#endif	/* SIM */


/* Define two different config functions. One for the daemon and the other for
 * the simulator. The simulator ignores a lot of the standard ntpd configuration
 * options
 */
#ifndef SIM
static void
config_ntpd(
	config_tree *ptree,
	int/*BOOL*/ input_from_files
	)
{
	/* [Bug 3435] check and esure clock sanity if configured from
	 * file and clock sanity parameters (-> basedate) are given. Do
	 * this ASAP, so we don't disturb the closed loop controller.
	 */
	if (input_from_files) {
		if (config_tos_clock(ptree))
			clamp_systime();
	}

	config_nic_rules(ptree, input_from_files);
	config_monitor(ptree);
	config_auth(ptree);
	config_tos(ptree);
	config_access(ptree);
	config_tinker(ptree);
	config_rlimit(ptree);
	config_system_opts(ptree);
	config_logconfig(ptree);
	config_phone(ptree);
	config_mdnstries(ptree);
	config_setvar(ptree);
	config_ttl(ptree);
	config_vars(ptree);

	io_open_sockets();	/* [bug 2837] dep. on config_vars() */

	config_trap(ptree);	/* [bug 2923] dep. on io_open_sockets() */
	config_other_modes(ptree);
	config_peers(ptree);
	config_unpeers(ptree);
	config_fudge(ptree);
	config_reset_counters(ptree);

#ifdef DEBUG
	if (debug > 1) {
		dump_restricts();
	}
#endif

#ifdef TEST_BLOCKING_WORKER
	{
		struct addrinfo hints;

		ZERO(hints);
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;
		getaddrinfo_sometime("www.cnn.com", "ntp", &hints,
				     INITIAL_DNS_RETRY,
				     gai_test_callback, (void *)1);
		hints.ai_family = AF_INET6;
		getaddrinfo_sometime("ipv6.google.com", "ntp", &hints,
				     INITIAL_DNS_RETRY,
				     gai_test_callback, (void *)0x600);
	}
#endif
}
#endif	/* !SIM */


#ifdef SIM
static void
config_ntpdsim(
	config_tree *ptree
	)
{
	printf("Configuring Simulator...\n");
	printf("Some ntpd-specific commands in the configuration file will be ignored.\n");

	config_tos(ptree);
	config_monitor(ptree);
	config_tinker(ptree);
	if (0)
		config_rlimit(ptree);	/* not needed for the simulator */
	config_system_opts(ptree);
	config_logconfig(ptree);
	config_vars(ptree);
	config_sim(ptree);
}
#endif /* SIM */


/*
 * config_remotely() - implements ntpd side of ntpq :config
 */
void
config_remotely(
	sockaddr_u *	remote_addr
	)
{
	char origin[128];

	snprintf(origin, sizeof(origin), "remote config from %s",
		 stoa(remote_addr));
	lex_init_stack(origin, NULL); /* no checking needed... */
	init_syntax_tree(&cfgt);
	yyparse();
	lex_drop_stack();

	cfgt.source.attr = CONF_SOURCE_NTPQ;
	cfgt.timestamp = time(NULL);
	cfgt.source.value.s = estrdup(stoa(remote_addr));

	DPRINTF(1, ("Finished Parsing!!\n"));

	save_and_apply_config_tree(FALSE);
}


/*
 * getconfig() - process startup configuration file e.g /etc/ntp.conf
 */
void
getconfig(
	int	argc,
	char **	argv
	)
{
	char	line[256];

#ifdef DEBUG
	atexit(free_all_config_trees);
#endif
#ifndef SYS_WINNT
	config_file = CONFIG_FILE;
#else
	temp = CONFIG_FILE;
	if (!ExpandEnvironmentStringsA(temp, config_file_storage,
				       sizeof(config_file_storage))) {
		msyslog(LOG_ERR, "ExpandEnvironmentStrings CONFIG_FILE failed: %m");
		exit(1);
	}
	config_file = config_file_storage;

	temp = ALT_CONFIG_FILE;
	if (!ExpandEnvironmentStringsA(temp, alt_config_file_storage,
				       sizeof(alt_config_file_storage))) {
		msyslog(LOG_ERR, "ExpandEnvironmentStrings ALT_CONFIG_FILE failed: %m");
		exit(1);
	}
	alt_config_file = alt_config_file_storage;
#endif /* SYS_WINNT */

	/*
	 * install a non default variable with this daemon version
	 */
	snprintf(line, sizeof(line), "daemon_version=\"%s\"", Version);
	set_sys_var(line, strlen(line) + 1, RO);

	/*
	 * Set up for the first time step to install a variable showing
	 * which syscall is being used to step.
	 */
	set_tod_using = &ntpd_set_tod_using;

	getCmdOpts(argc, argv);
	init_syntax_tree(&cfgt);
	if (
		!lex_init_stack(FindConfig(config_file), "r")
#ifdef HAVE_NETINFO
		/* If there is no config_file, try NetInfo. */
		&& check_netinfo && !(config_netinfo = get_netinfo_config())
#endif /* HAVE_NETINFO */
		) {
		msyslog(LOG_INFO, "getconfig: Couldn't open <%s>: %m", FindConfig(config_file));
#ifndef SYS_WINNT
		io_open_sockets();

		return;
#else
		/* Under WinNT try alternate_config_file name, first NTP.CONF, then NTP.INI */

		if (!lex_init_stack(FindConfig(alt_config_file), "r"))  {
			/*
			 * Broadcast clients can sometimes run without
			 * a configuration file.
			 */
			msyslog(LOG_INFO, "getconfig: Couldn't open <%s>: %m", FindConfig(alt_config_file));
			io_open_sockets();

			return;
		}
		cfgt.source.value.s = estrdup(alt_config_file);
#endif	/* SYS_WINNT */
	} else
		cfgt.source.value.s = estrdup(config_file);


	/*** BULK OF THE PARSER ***/
#ifdef DEBUG
	yydebug = !!(debug >= 5);
#endif
	yyparse();
	lex_drop_stack();

	DPRINTF(1, ("Finished Parsing!!\n"));

	cfgt.source.attr = CONF_SOURCE_FILE;
	cfgt.timestamp = time(NULL);

	save_and_apply_config_tree(TRUE);

#ifdef HAVE_NETINFO
	if (config_netinfo)
		free_netinfo_config(config_netinfo);
#endif /* HAVE_NETINFO */
}


void
save_and_apply_config_tree(int/*BOOL*/ input_from_file)
{
	config_tree *ptree;
#ifndef SAVECONFIG
	config_tree *punlinked;
#endif

	/*
	 * Keep all the configuration trees applied since startup in
	 * a list that can be used to dump the configuration back to
	 * a text file.
	 */
	ptree = emalloc(sizeof(*ptree));
	memcpy(ptree, &cfgt, sizeof(*ptree));
	ZERO(cfgt);

	LINK_TAIL_SLIST(cfg_tree_history, ptree, link, config_tree);

#ifdef SAVECONFIG
	if (HAVE_OPT( SAVECONFIGQUIT )) {
		FILE *dumpfile;
		int err;
		int dumpfailed;

		dumpfile = fopen(OPT_ARG( SAVECONFIGQUIT ), "w");
		if (NULL == dumpfile) {
			err = errno;
			mfprintf(stderr,
				 "can not create save file %s, error %d %m\n",
				 OPT_ARG(SAVECONFIGQUIT), err);
			exit(err);
		}

		dumpfailed = dump_all_config_trees(dumpfile, 0);
		if (dumpfailed)
			fprintf(stderr,
				"--saveconfigquit %s error %d\n",
				OPT_ARG( SAVECONFIGQUIT ),
				dumpfailed);
		else
			fprintf(stderr,
				"configuration saved to %s\n",
				OPT_ARG( SAVECONFIGQUIT ));

		exit(dumpfailed);
	}
#endif	/* SAVECONFIG */

	/* The actual configuration done depends on whether we are configuring the
	 * simulator or the daemon. Perform a check and call the appropriate
	 * function as needed.
	 */

#ifndef SIM
	config_ntpd(ptree, input_from_file);
#else
	config_ntpdsim(ptree);
#endif

	/*
	 * With configure --disable-saveconfig, there's no use keeping
	 * the config tree around after application, so free it.
	 */
#ifndef SAVECONFIG
	UNLINK_SLIST(punlinked, cfg_tree_history, ptree, link,
		     config_tree);
	INSIST(punlinked == ptree);
	free_config_tree(ptree);
#endif
}

/* Hack to disambiguate 'server' statements for refclocks and network peers.
 * Please note the qualification 'hack'. It's just that.
 */
static int/*BOOL*/
is_refclk_addr(
	const address_node * addr
	)
{
	return addr && addr->address && !strncmp(addr->address, "127.127.", 8);
}

static void
ntpd_set_tod_using(
	const char *which
	)
{
	char line[128];

	snprintf(line, sizeof(line), "settimeofday=\"%s\"", which);
	set_sys_var(line, strlen(line) + 1, RO);
}


static char *
normal_dtoa(
	double d
	)
{
	char *	buf;
	char *	pch_e;
	char *	pch_nz;

	LIB_GETBUF(buf);
	snprintf(buf, LIB_BUFLENGTH, "%g", d);

	/* use lowercase 'e', strip any leading zeroes in exponent */
	pch_e = strchr(buf, 'e');
	if (NULL == pch_e) {
		pch_e = strchr(buf, 'E');
		if (NULL == pch_e)
			return buf;
		*pch_e = 'e';
	}
	pch_e++;
	if ('-' == *pch_e)
		pch_e++;
	pch_nz = pch_e;
	while ('0' == *pch_nz)
		pch_nz++;
	if (pch_nz == pch_e)
		return buf;
	strlcpy(pch_e, pch_nz, LIB_BUFLENGTH - (pch_e - buf));

	return buf;
}


/* FUNCTIONS COPIED FROM THE OLDER ntp_config.c
 * --------------------------------------------
 */


/*
 * get_pfxmatch - find value for prefixmatch
 * and update char * accordingly
 */
static u_int32
get_pfxmatch(
	const char **	pstr,
	struct masks *	m
	)
{
	while (m->name != NULL) {
		if (strncmp(*pstr, m->name, strlen(m->name)) == 0) {
			*pstr += strlen(m->name);
			return m->mask;
		} else {
			m++;
		}
	}
	return 0;
}

/*
 * get_match - find logmask value
 */
static u_int32
get_match(
	const char *	str,
	struct masks *	m
	)
{
	while (m->name != NULL) {
		if (strcmp(str, m->name) == 0)
			return m->mask;
		else
			m++;
	}
	return 0;
}

/*
 * get_logmask - build bitmask for ntp_syslogmask
 */
static u_int32
get_logmask(
	const char *	str
	)
{
	const char *	t;
	u_int32		offset;
	u_int32		mask;

	mask = get_match(str, logcfg_noclass_items);
	if (mask != 0)
		return mask;

	t = str;
	offset = get_pfxmatch(&t, logcfg_class);
	mask   = get_match(t, logcfg_class_items);

	if (mask)
		return mask << offset;
	else
		msyslog(LOG_ERR, "logconfig: '%s' not recognized - ignored",
			str);

	return 0;
}


#ifdef HAVE_NETINFO

/*
 * get_netinfo_config - find the nearest NetInfo domain with an ntp
 * configuration and initialize the configuration state.
 */
static struct netinfo_config_state *
get_netinfo_config(void)
{
	ni_status status;
	void *domain;
	ni_id config_dir;
	struct netinfo_config_state *config;

	if (ni_open(NULL, ".", &domain) != NI_OK) return NULL;

	while ((status = ni_pathsearch(domain, &config_dir, NETINFO_CONFIG_DIR)) == NI_NODIR) {
		void *next_domain;
		if (ni_open(domain, "..", &next_domain) != NI_OK) {
			ni_free(next_domain);
			break;
		}
		ni_free(domain);
		domain = next_domain;
	}
	if (status != NI_OK) {
		ni_free(domain);
		return NULL;
	}

	config = emalloc(sizeof(*config));
	config->domain = domain;
	config->config_dir = config_dir;
	config->prop_index = 0;
	config->val_index = 0;
	config->val_list = NULL;

	return config;
}


/*
 * free_netinfo_config - release NetInfo configuration state
 */
static void
free_netinfo_config(
	struct netinfo_config_state *config
	)
{
	ni_free(config->domain);
	free(config);
}


/*
 * gettokens_netinfo - return tokens from NetInfo
 */
static int
gettokens_netinfo (
	struct netinfo_config_state *config,
	char **tokenlist,
	int *ntokens
	)
{
	int prop_index = config->prop_index;
	int val_index = config->val_index;
	char **val_list = config->val_list;

	/*
	 * Iterate through each keyword and look for a property that matches it.
	 */
  again:
	if (!val_list) {
		for (; prop_index < COUNTOF(keywords); prop_index++)
		{
			ni_namelist namelist;
			struct keyword current_prop = keywords[prop_index];
			ni_index index;

			/*
			 * For each value associated in the property, we're going to return
			 * a separate line. We squirrel away the values in the config state
			 * so the next time through, we don't need to do this lookup.
			 */
			NI_INIT(&namelist);
			if (NI_OK == ni_lookupprop(config->domain,
			    &config->config_dir, current_prop.text,
			    &namelist)) {

				/* Found the property, but it has no values */
				if (namelist.ni_namelist_len == 0) continue;

				config->val_list =
				    eallocarray(
					(namelist.ni_namelist_len + 1),
					sizeof(char*));
				val_list = config->val_list;

				for (index = 0;
				     index < namelist.ni_namelist_len;
				     index++) {
					char *value;

					value = namelist.ni_namelist_val[index];
					val_list[index] = estrdup(value);
				}
				val_list[index] = NULL;

				break;
			}
			ni_namelist_free(&namelist);
		}
		config->prop_index = prop_index;
	}

	/* No list; we're done here. */
	if (!val_list)
		return CONFIG_UNKNOWN;

	/*
	 * We have a list of values for the current property.
	 * Iterate through them and return each in order.
	 */
	if (val_list[val_index]) {
		int ntok = 1;
		int quoted = 0;
		char *tokens = val_list[val_index];

		msyslog(LOG_INFO, "%s %s", keywords[prop_index].text, val_list[val_index]);

		(const char*)tokenlist[0] = keywords[prop_index].text;
		for (ntok = 1; ntok < MAXTOKENS; ntok++) {
			tokenlist[ntok] = tokens;
			while (!ISEOL(*tokens) && (!ISSPACE(*tokens) || quoted))
				quoted ^= (*tokens++ == '"');

			if (ISEOL(*tokens)) {
				*tokens = '\0';
				break;
			} else {		/* must be space */
				*tokens++ = '\0';
				while (ISSPACE(*tokens))
					tokens++;
				if (ISEOL(*tokens))
					break;
			}
		}

		if (ntok == MAXTOKENS) {
			/* HMS: chomp it to lose the EOL? */
			msyslog(LOG_ERR,
				"gettokens_netinfo: too many tokens.  Ignoring: %s",
				tokens);
		} else {
			*ntokens = ntok + 1;
		}

		config->val_index++;	/* HMS: Should this be in the 'else'? */

		return keywords[prop_index].keytype;
	}

	/* We're done with the current property. */
	prop_index = ++config->prop_index;

	/* Free val_list and reset counters. */
	for (val_index = 0; val_list[val_index]; val_index++)
		free(val_list[val_index]);
	free(val_list);
	val_list = config->val_list = NULL;
	val_index = config->val_index = 0;

	goto again;
}
#endif /* HAVE_NETINFO */


/*
 * getnetnum - return a net number (this is crude, but careful)
 *
 * returns 1 for success, and mysteriously, 0 for most failures, and
 * -1 if the address found is IPv6 and we believe IPv6 isn't working.
 */
#ifndef SIM
static int
getnetnum(
	const char *num,
	sockaddr_u *addr,
	int complain,
	enum gnn_type a_type	/* ignored */
	)
{
	REQUIRE(AF_UNSPEC == AF(addr) ||
		AF_INET == AF(addr) ||
		AF_INET6 == AF(addr));

	if (!is_ip_address(num, AF(addr), addr))
		return 0;

	if (IS_IPV6(addr) && !ipv6_works)
		return -1;

# ifdef ISC_PLATFORM_HAVESALEN
	addr->sa.sa_len = SIZEOF_SOCKADDR(AF(addr));
# endif
	SET_PORT(addr, NTP_PORT);

	DPRINTF(2, ("getnetnum given %s, got %s\n", num, stoa(addr)));

	return 1;
}
#endif	/* !SIM */

#if defined(HAVE_SETRLIMIT)
void
ntp_rlimit(
	int	rl_what,
	rlim_t	rl_value,
	int	rl_scale,
	const char *	rl_sstr
	)
{
	struct rlimit	rl;

	switch (rl_what) {
# ifdef RLIMIT_MEMLOCK
	    case RLIMIT_MEMLOCK:
		if (HAVE_OPT( SAVECONFIGQUIT )) {
			break;
		}
		/*
		 * The default RLIMIT_MEMLOCK is very low on Linux systems.
		 * Unless we increase this limit malloc calls are likely to
		 * fail if we drop root privilege.  To be useful the value
		 * has to be larger than the largest ntpd resident set size.
		 */
		DPRINTF(2, ("ntp_rlimit: MEMLOCK: %d %s\n",
			(int)(rl_value / rl_scale), rl_sstr));
		rl.rlim_cur = rl.rlim_max = rl_value;
		if (setrlimit(RLIMIT_MEMLOCK, &rl) == -1)
			msyslog(LOG_ERR, "Cannot set RLIMIT_MEMLOCK: %m");
		break;
# endif /* RLIMIT_MEMLOCK */

# ifdef RLIMIT_NOFILE
	    case RLIMIT_NOFILE:
		/*
		 * For large systems the default file descriptor limit may
		 * not be enough.
		 */
		DPRINTF(2, ("ntp_rlimit: NOFILE: %d %s\n",
			(int)(rl_value / rl_scale), rl_sstr));
		rl.rlim_cur = rl.rlim_max = rl_value;
		if (setrlimit(RLIMIT_NOFILE, &rl) == -1)
			msyslog(LOG_ERR, "Cannot set RLIMIT_NOFILE: %m");
		break;
# endif /* RLIMIT_NOFILE */

# ifdef RLIMIT_STACK
	    case RLIMIT_STACK:
		/*
		 * Provide a way to set the stack limit to something
		 * smaller, so that we don't lock a lot of unused
		 * stack memory.
		 */
		DPRINTF(2, ("ntp_rlimit: STACK: %d %s pages\n",
			    (int)(rl_value / rl_scale), rl_sstr));
		if (-1 == getrlimit(RLIMIT_STACK, &rl)) {
			msyslog(LOG_ERR, "getrlimit(RLIMIT_STACK) failed: %m");
		} else {
			if (rl_value > rl.rlim_max) {
				msyslog(LOG_WARNING,
					"ntp_rlimit: using maximum allowed stack limit %lu instead of %lu.",
					(u_long)rl.rlim_max,
					(u_long)rl_value);
				rl_value = rl.rlim_max;
			}
			rl.rlim_cur = rl_value;
			if (-1 == setrlimit(RLIMIT_STACK, &rl)) {
				msyslog(LOG_ERR,
					"ntp_rlimit: Cannot set RLIMIT_STACK: %m");
			}
		}
		break;
# endif /* RLIMIT_STACK */

	    default:
		    fatal_error("ntp_rlimit: unexpected RLIMIT case: %d", rl_what);
	}
}
#endif	/* HAVE_SETRLIMIT */


char *
build_iflags(u_int32 iflags)
{
	static char ifs[1024];

	ifs[0] = '\0';

	if (iflags & INT_UP) {
		iflags &= ~INT_UP;
		appendstr(ifs, sizeof ifs, "up");
	}

	if (iflags & INT_PPP) {
		iflags &= ~INT_PPP;
		appendstr(ifs, sizeof ifs, "ppp");
	}

	if (iflags & INT_LOOPBACK) {
		iflags &= ~INT_LOOPBACK;
		appendstr(ifs, sizeof ifs, "loopback");
	}

	if (iflags & INT_BROADCAST) {
		iflags &= ~INT_BROADCAST;
		appendstr(ifs, sizeof ifs, "broadcast");
	}

	if (iflags & INT_MULTICAST) {
		iflags &= ~INT_MULTICAST;
		appendstr(ifs, sizeof ifs, "multicast");
	}

	if (iflags & INT_BCASTOPEN) {
		iflags &= ~INT_BCASTOPEN;
		appendstr(ifs, sizeof ifs, "bcastopen");
	}

	if (iflags & INT_MCASTOPEN) {
		iflags &= ~INT_MCASTOPEN;
		appendstr(ifs, sizeof ifs, "mcastopen");
	}

	if (iflags & INT_WILDCARD) {
		iflags &= ~INT_WILDCARD;
		appendstr(ifs, sizeof ifs, "wildcard");
	}

	if (iflags & INT_MCASTIF) {
		iflags &= ~INT_MCASTIF;
		appendstr(ifs, sizeof ifs, "MCASTif");
	}

	if (iflags & INT_PRIVACY) {
		iflags &= ~INT_PRIVACY;
		appendstr(ifs, sizeof ifs, "IPv6privacy");
	}

	if (iflags & INT_BCASTXMIT) {
		iflags &= ~INT_BCASTXMIT;
		appendstr(ifs, sizeof ifs, "bcastxmit");
	}

	if (iflags) {
		char string[10];

		snprintf(string, sizeof string, "%0x", iflags);
		appendstr(ifs, sizeof ifs, string);
	}

	return ifs;
}


char *
build_mflags(u_short mflags)
{
	static char mfs[1024];

	mfs[0] = '\0';

	if (mflags & RESM_NTPONLY) {
		mflags &= ~RESM_NTPONLY;
		appendstr(mfs, sizeof mfs, "ntponly");
	}

	if (mflags & RESM_SOURCE) {
		mflags &= ~RESM_SOURCE;
		appendstr(mfs, sizeof mfs, "source");
	}

	if (mflags) {
		char string[10];

		snprintf(string, sizeof string, "%0x", mflags);
		appendstr(mfs, sizeof mfs, string);
	}

	return mfs;
}


char *
build_rflags(u_short rflags)
{
	static char rfs[1024];

	rfs[0] = '\0';

	if (rflags & RES_FLAKE) {
		rflags &= ~RES_FLAKE;
		appendstr(rfs, sizeof rfs, "flake");
	}

	if (rflags & RES_IGNORE) {
		rflags &= ~RES_IGNORE;
		appendstr(rfs, sizeof rfs, "ignore");
	}

	if (rflags & RES_KOD) {
		rflags &= ~RES_KOD;
		appendstr(rfs, sizeof rfs, "kod");
	}

	if (rflags & RES_MSSNTP) {
		rflags &= ~RES_MSSNTP;
		appendstr(rfs, sizeof rfs, "mssntp");
	}

	if (rflags & RES_LIMITED) {
		rflags &= ~RES_LIMITED;
		appendstr(rfs, sizeof rfs, "limited");
	}

	if (rflags & RES_LPTRAP) {
		rflags &= ~RES_LPTRAP;
		appendstr(rfs, sizeof rfs, "lptrap");
	}

	if (rflags & RES_NOMODIFY) {
		rflags &= ~RES_NOMODIFY;
		appendstr(rfs, sizeof rfs, "nomodify");
	}

	if (rflags & RES_NOMRULIST) {
		rflags &= ~RES_NOMRULIST;
		appendstr(rfs, sizeof rfs, "nomrulist");
	}

	if (rflags & RES_NOEPEER) {
		rflags &= ~RES_NOEPEER;
		appendstr(rfs, sizeof rfs, "noepeer");
	}

	if (rflags & RES_NOPEER) {
		rflags &= ~RES_NOPEER;
		appendstr(rfs, sizeof rfs, "nopeer");
	}

	if (rflags & RES_NOQUERY) {
		rflags &= ~RES_NOQUERY;
		appendstr(rfs, sizeof rfs, "noquery");
	}

	if (rflags & RES_DONTSERVE) {
		rflags &= ~RES_DONTSERVE;
		appendstr(rfs, sizeof rfs, "dontserve");
	}

	if (rflags & RES_NOTRAP) {
		rflags &= ~RES_NOTRAP;
		appendstr(rfs, sizeof rfs, "notrap");
	}

	if (rflags & RES_DONTTRUST) {
		rflags &= ~RES_DONTTRUST;
		appendstr(rfs, sizeof rfs, "notrust");
	}

	if (rflags & RES_VERSION) {
		rflags &= ~RES_VERSION;
		appendstr(rfs, sizeof rfs, "version");
	}

	if (rflags) {
		char string[10];

		snprintf(string, sizeof string, "%0x", rflags);
		appendstr(rfs, sizeof rfs, string);
	}

	if ('\0' == rfs[0]) {
		appendstr(rfs, sizeof rfs, "(none)");
	}

	return rfs;
}


static void
appendstr(
	char *string,
	size_t s,
	const char *new
	)
{
	if (*string != '\0') {
		(void)strlcat(string, ",", s);
	}
	(void)strlcat(string, new, s);

	return;
}
