#include <sys/param.h>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <err.h>
#include <sysexits.h>

#include <netsmb/smb_lib.h>
#include <netsmb/smb_conn.h>

#include "common.h"

extern char *__progname;

static void help(void);
static void help_usage(void);
static int  cmd_crypt(int argc, char *argv[]);
static int  cmd_help(int argc, char *argv[]);

int verbose;

typedef int cmd_fn_t (int argc, char *argv[]);
typedef void cmd_usage_t (void);

#define	CMDFL_NO_KMOD	0x0001

static struct commands {
	const char *	name;
	cmd_fn_t*	fn;
	cmd_usage_t *	usage;
	int 		flags;
} commands[] = {
	{"crypt",	cmd_crypt,	NULL, CMDFL_NO_KMOD},
	{"help",	cmd_help,	help_usage, CMDFL_NO_KMOD},
	{"lc",		cmd_dumptree,	NULL},
	{"login",	cmd_login,	login_usage},
	{"logout",	cmd_logout,	logout_usage},
	{"lookup",	cmd_lookup,	lookup_usage, CMDFL_NO_KMOD},
	{"print",	cmd_print,	print_usage},
	{"view",	cmd_view,	view_usage},
	{NULL, NULL}
};

static struct commands *
lookupcmd(const char *name)
{
	struct commands *cmd;

	for (cmd = commands; cmd->name; cmd++) {
		if (strcmp(cmd->name, name) == 0)
			return cmd;
	}
	return NULL;
}

int
cmd_crypt(int argc, char *argv[])
{
	char *cp, *psw;
    
	if (argc < 2)
		psw = getpass("Password:");
	else
		psw = argv[1];
	cp = smb_simplecrypt(NULL, psw);
	if (cp == NULL)
		errx(EX_DATAERR, "out of memory");
	printf("%s\n", cp);
	free(cp);
	exit(0);
}

int
cmd_help(int argc, char *argv[])
{
	struct commands *cmd;
	char *cp;
    
	if (argc < 2)
		help_usage();
	cp = argv[1];
	cmd = lookupcmd(cp);
	if (cmd == NULL)
		errx(EX_DATAERR, "unknown command %s", cp);
	if (cmd->usage == NULL)
		errx(EX_DATAERR, "no specific help for command %s", cp);
	cmd->usage();
	exit(0);
}

int
main(int argc, char *argv[])
{
	struct commands *cmd;
	char *cp;
	int opt;
#ifdef APPLE
        extern void dropsuid();

	dropsuid();
#endif /* APPLE */

	if (argc < 2)
		help();

	while ((opt = getopt(argc, argv, "hv")) != EOF) {
		switch (opt) {
		    case 'h':
			help();
			/*NOTREACHED */
		    case 'v':
			verbose = 1;
			break;
		    default:
			warnx("invalid option %c", opt);
			help();
			/*NOTREACHED */
		}
	}
	if (optind >= argc)
		help();

	cp = argv[optind];
	cmd = lookupcmd(cp);
	if (cmd == NULL)
		errx(EX_DATAERR, "unknown command %s", cp);

	if ((cmd->flags & CMDFL_NO_KMOD) == 0 && smb_lib_init() != 0)
		exit(1);

	argc -= optind;
	argv += optind;
	optind = optreset = 1;
	return cmd->fn(argc, argv);
}

static void
help(void) {
	printf("\n");
	printf("usage: %s [-hv] command [args]\n", __progname);
	printf("where commands are:\n"
	" crypt [password]		slightly encrypt password\n"
	" help command			display help on \"command\"\n"
	" lc 				display active connections\n"
	" login //user@host[/share]	login to the specified host\n"
	" logout //user@host[/share]	logout from the specified host\n"
	" print //user@host/share file	print file to the specified remote printer\n"
	" view //user@host		list resources on the specified host\n"
	"\n");
	exit(1);
}

static void
help_usage(void) {
	printf("usage: smbutil help command\n");
	exit(1);
}
