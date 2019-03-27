/*
 * cmd_args.c = command-line argument processing
 */
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "ntpd.h"
#include "ntp_stdlib.h"
#include "ntp_config.h"
#include "ntp_cmdargs.h"

#include "ntpd-opts.h"

/*
 * Definitions of things either imported from or exported to outside
 */
extern char const *progname;

#ifdef HAVE_NETINFO
extern int	check_netinfo;
#endif


/*
 * getCmdOpts - apply most command line options
 *
 * A few options are examined earlier in ntpd.c ntpdmain() and
 * ports/winnt/ntpd/ntservice.c main().
 */
void
getCmdOpts(
	int	argc,
	char **	argv
	)
{
	extern const char *config_file;
	int errflg;

	/*
	 * Initialize, initialize
	 */
	errflg = 0;

	if (ipv4_works && ipv6_works) {
		if (HAVE_OPT( IPV4 ))
			ipv6_works = 0;
		else if (HAVE_OPT( IPV6 ))
			ipv4_works = 0;
	} else if (!ipv4_works && !ipv6_works) {
		msyslog(LOG_ERR, "Neither IPv4 nor IPv6 networking detected, fatal.");
		exit(1);
	} else if (HAVE_OPT( IPV4 ) && !ipv4_works)
		msyslog(LOG_WARNING, "-4/--ipv4 ignored, IPv4 networking not found.");
	else if (HAVE_OPT( IPV6 ) && !ipv6_works)
		msyslog(LOG_WARNING, "-6/--ipv6 ignored, IPv6 networking not found.");

	if (HAVE_OPT( AUTHREQ ))
		proto_config(PROTO_AUTHENTICATE, 1, 0., NULL);
	else if (HAVE_OPT( AUTHNOREQ ))
		proto_config(PROTO_AUTHENTICATE, 0, 0., NULL);

	if (HAVE_OPT( BCASTSYNC ))
		proto_config(PROTO_BROADCLIENT, 1, 0., NULL);

	if (HAVE_OPT( CONFIGFILE )) {
		config_file = OPT_ARG( CONFIGFILE );
#ifdef HAVE_NETINFO
		check_netinfo = 0;
#endif
	}

	if (HAVE_OPT( DRIFTFILE ))
		stats_config(STATS_FREQ_FILE, OPT_ARG( DRIFTFILE ));

	if (HAVE_OPT( PANICGATE ))
		allow_panic = TRUE;

	if (HAVE_OPT( FORCE_STEP_ONCE ))
		force_step_once = TRUE;

#ifdef HAVE_DROPROOT
	if (HAVE_OPT( JAILDIR )) {
		droproot = 1;
		chrootdir = OPT_ARG( JAILDIR );
	}
#endif

	if (HAVE_OPT( KEYFILE ))
		getauthkeys(OPT_ARG( KEYFILE ));

	if (HAVE_OPT( PIDFILE ))
		stats_config(STATS_PID_FILE, OPT_ARG( PIDFILE ));

	if (HAVE_OPT( QUIT ))
		mode_ntpdate = TRUE;

	if (HAVE_OPT( PROPAGATIONDELAY ))
		do {
			double tmp;
			const char *my_ntp_optarg = OPT_ARG( PROPAGATIONDELAY );

			if (sscanf(my_ntp_optarg, "%lf", &tmp) != 1) {
				msyslog(LOG_ERR,
					"command line broadcast delay value %s undecodable",
					my_ntp_optarg);
			} else {
				proto_config(PROTO_BROADDELAY, 0, tmp, NULL);
			}
		} while (0);

	if (HAVE_OPT( STATSDIR ))
		stats_config(STATS_STATSDIR, OPT_ARG( STATSDIR ));

	if (HAVE_OPT( TRUSTEDKEY )) {
		int		ct = STACKCT_OPT(  TRUSTEDKEY );
		const char**	pp = STACKLST_OPT( TRUSTEDKEY );

		do  {
			u_long tkey;
			const char* p = *pp++;

			tkey = (int)atol(p);
			if (tkey == 0 || tkey > NTP_MAXKEY) {
				msyslog(LOG_ERR,
				    "command line trusted key %s is invalid",
				    p);
			} else {
				authtrust(tkey, 1);
			}
		} while (--ct > 0);
	}

#ifdef HAVE_DROPROOT
	if (HAVE_OPT( USER )) {
		droproot = 1;
		user = estrdup(OPT_ARG( USER ));
		group = strrchr(user, ':');
		if (group != NULL) {
			size_t	len;

			*group++ = '\0'; /* get rid of the ':' */
			len = group - user;
			group = estrdup(group);
			user = erealloc(user, len);
		}
	}
#endif

	if (HAVE_OPT( VAR )) {
		int		ct;
		const char **	pp;
		const char *	v_assign;

		ct = STACKCT_OPT(  VAR );
		pp = STACKLST_OPT( VAR );

		do  {
			v_assign = *pp++;
			set_sys_var(v_assign, strlen(v_assign) + 1, RW);
		} while (--ct > 0);
	}

	if (HAVE_OPT( DVAR )) {
		int		ct = STACKCT_OPT(  DVAR );
		const char**	pp = STACKLST_OPT( DVAR );

		do  {
			const char* my_ntp_optarg = *pp++;

			set_sys_var(my_ntp_optarg, strlen(my_ntp_optarg)+1,
			    (u_short) (RW | DEF));
		} while (--ct > 0);
	}

	if (HAVE_OPT( SLEW ))
		loop_config(LOOP_MAX, 600);

	if (HAVE_OPT( UPDATEINTERVAL )) {
		long val = OPT_VALUE_UPDATEINTERVAL;

		if (val >= 0)
			interface_interval = val;
		else {
			fprintf(stderr,
				"command line interface update interval %ld must not be negative\n",
				val);
			msyslog(LOG_ERR,
				"command line interface update interval %ld must not be negative",
				val);
			errflg++;
		}
	}


	/* save list of servers from cmd line for config_peers() use */
	if (argc > 0) {
		cmdline_server_count = argc;
		cmdline_servers = argv;
	}

	/* display usage & exit with any option processing errors */
	if (errflg)
		optionUsage(&ntpdOptions, 2);	/* does not return */
}
