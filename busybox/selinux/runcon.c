/*
 * runcon [ context |
 *         ( [ -c ] [ -r role ] [-t type] [ -u user ] [ -l levelrange ] )
 *         command [arg1 [arg2 ...] ]
 *
 * attempt to run the specified command with the specified context.
 *
 * -r role  : use the current context with the specified role
 * -t type  : use the current context with the specified type
 * -u user  : use the current context with the specified user
 * -l level : use the current context with the specified level range
 * -c       : compute process transition context before modifying
 *
 * Contexts are interpreted as follows:
 *
 * Number of       MLS
 * components    system?
 *
 *     1            -         type
 *     2            -         role:type
 *     3            Y         role:type:range
 *     3            N         user:role:type
 *     4            Y         user:role:type:range
 *     4            N         error
 *
 * Port to busybox: KaiGai Kohei <kaigai@kaigai.gr.jp>
 *                  - based on coreutils-5.97 (in Fedora Core 6)
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config RUNCON
//config:	bool "runcon (6.6 kb)"
//config:	default n
//config:	depends on SELINUX
//config:	help
//config:	Enable support to run command in specified security context.

//applet:IF_RUNCON(APPLET(runcon, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_RUNCON) += runcon.o

//usage:#define runcon_trivial_usage
//usage:       "[-c] [-u USER] [-r ROLE] [-t TYPE] [-l RANGE] PROG ARGS\n"
//usage:       "runcon CONTEXT PROG ARGS"
//usage:#define runcon_full_usage "\n\n"
//usage:       "Run PROG in a different security context\n"
//usage:     "\n	CONTEXT		Complete security context\n"
//usage:     "\n	-c	Compute process transition context before modifying"
//usage:     "\n	-t TYPE	Type (for same role as parent)"
//usage:     "\n	-u USER	User identity"
//usage:     "\n	-r ROLE	Role"
//usage:     "\n	-l RNG	Levelrange"

#include <selinux/context.h>
/* from deprecated <selinux/flask.h>: */
#undef  SECCLASS_PROCESS
#define SECCLASS_PROCESS 2

#include "libbb.h"

static context_t runcon_compute_new_context(char *user, char *role, char *type, char *range,
			char *command, int compute_trans)
{
	context_t con;
	security_context_t cur_context;

	if (getcon(&cur_context))
		bb_error_msg_and_die("can't get current context");

	if (compute_trans) {
		security_context_t file_context, new_context;

		if (getfilecon(command, &file_context) < 0)
			bb_error_msg_and_die("can't retrieve attributes of '%s'",
					command);
		if (security_compute_create(cur_context, file_context,
					SECCLASS_PROCESS, &new_context))
			bb_error_msg_and_die("unable to compute a new context");
		cur_context = new_context;
	}

	con = context_new(cur_context);
	if (!con)
		bb_error_msg_and_die("'%s' is not a valid context", cur_context);
	if (user && context_user_set(con, user))
		bb_error_msg_and_die("can't set new user '%s'", user);
	if (type && context_type_set(con, type))
		bb_error_msg_and_die("can't set new type '%s'", type);
	if (range && context_range_set(con, range))
		bb_error_msg_and_die("can't set new range '%s'", range);
	if (role && context_role_set(con, role))
		bb_error_msg_and_die("can't set new role '%s'", role);

	return con;
}

#if ENABLE_LONG_OPTS
static const char runcon_longopts[] ALIGN1 =
	"user\0"    Required_argument "u"
	"role\0"    Required_argument "r"
	"type\0"    Required_argument "t"
	"range\0"   Required_argument "l"
	"compute\0" No_argument "c"
	"help\0"    No_argument "h"
	;
#endif

#define OPTS_ROLE	(1<<0)	/* r */
#define OPTS_TYPE	(1<<1)	/* t */
#define OPTS_USER	(1<<2)	/* u */
#define OPTS_RANGE	(1<<3)	/* l */
#define OPTS_COMPUTE	(1<<4)	/* c */
#define OPTS_HELP	(1<<5)	/* h */
#define OPTS_CONTEXT_COMPONENT		(OPTS_ROLE | OPTS_TYPE | OPTS_USER | OPTS_RANGE)

int runcon_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int runcon_main(int argc UNUSED_PARAM, char **argv)
{
	char *role = NULL;
	char *range = NULL;
	char *user = NULL;
	char *type = NULL;
	char *context = NULL;
	unsigned opts;
	context_t con;

	selinux_or_die();

	opts = getopt32long(argv, "^"
			"r:t:u:l:ch"
			"\0" "-1",
			runcon_longopts,
			&role, &type, &user, &range
	);
	argv += optind;

	if (!(opts & OPTS_CONTEXT_COMPONENT)) {
		context = *argv++;
		if (!argv[0])
			bb_error_msg_and_die("no command given");
	}

	if (context) {
		con = context_new(context);
		if (!con)
			bb_error_msg_and_die("'%s' is not a valid context", context);
	} else {
		con = runcon_compute_new_context(user, role, type, range,
				argv[0], opts & OPTS_COMPUTE);
	}

	if (security_check_context(context_str(con)))
		bb_error_msg_and_die("'%s' is not a valid context",
				context_str(con));

	if (setexeccon(context_str(con)))
		bb_error_msg_and_die("can't set up security context '%s'",
				context_str(con));

	BB_EXECVP_or_die(argv);
}
