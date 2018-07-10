/*
 * sestatus -- displays the status of SELinux
 *
 * Ported to busybox: KaiGai Kohei <kaigai@ak.jp.nec.com>
 *
 * Copyright (C) KaiGai Kohei <kaigai@ak.jp.nec.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config SESTATUS
//config:	bool "sestatus (12 kb)"
//config:	default n
//config:	depends on SELINUX
//config:	help
//config:	Displays the status of SELinux.

//applet:IF_SESTATUS(APPLET(sestatus, BB_DIR_USR_SBIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_SESTATUS) += sestatus.o

//usage:#define sestatus_trivial_usage
//usage:       "[-vb]"
//usage:#define sestatus_full_usage "\n\n"
//usage:       "	-v	Verbose"
//usage:     "\n	-b	Display current state of booleans"

#include "libbb.h"

extern char *selinux_mnt;

#define OPT_VERBOSE  (1 << 0)
#define OPT_BOOLEAN  (1 << 1)

#define COL_FMT  "%-31s "

static void display_boolean(void)
{
	char **bools;
	int i, active, pending, nbool;

	if (security_get_boolean_names(&bools, &nbool) < 0)
		return;

	puts("\nPolicy booleans:");

	for (i = 0; i < nbool; i++) {
		active = security_get_boolean_active(bools[i]);
		if (active < 0)
			goto skip;
		pending = security_get_boolean_pending(bools[i]);
		if (pending < 0)
			goto skip;
		printf(COL_FMT "%s",
				bools[i], active == 0 ? "off" : "on");
		if (active != pending)
			printf(" (%sactivate pending)", pending == 0 ? "in" : "");
		bb_putchar('\n');
 skip:
		if (ENABLE_FEATURE_CLEAN_UP)
			free(bools[i]);
	}
	if (ENABLE_FEATURE_CLEAN_UP)
		free(bools);
}

static void read_config(char **pc, int npc, char **fc, int nfc)
{
	char *buf;
	parser_t *parser;
	int pc_ofs = 0, fc_ofs = 0, section = -1;

	pc[0] = fc[0] = NULL;

	parser = config_open("/etc/sestatus.conf");
	while (config_read(parser, &buf, 1, 1, "# \t", PARSE_NORMAL)) {
		if (strcmp(buf, "[process]") == 0) {
			section = 1;
		} else if (strcmp(buf, "[files]") == 0) {
			section = 2;
		} else {
			if (section == 1 && pc_ofs < npc -1) {
				pc[pc_ofs++] = xstrdup(buf);
				pc[pc_ofs] = NULL;
			} else if (section == 2 && fc_ofs < nfc - 1) {
				fc[fc_ofs++] = xstrdup(buf);
				fc[fc_ofs] = NULL;
			}
		}
	}
	config_close(parser);
}

static void display_verbose(void)
{
	security_context_t con, _con;
	char *fc[50], *pc[50], *cterm;
	pid_t *pidList;
	int i;

	read_config(pc, ARRAY_SIZE(pc), fc, ARRAY_SIZE(fc));

	/* process contexts */
	puts("\nProcess contexts:");

	/* current context */
	if (getcon(&con) == 0) {
		printf(COL_FMT "%s\n", "Current context:", con);
		if (ENABLE_FEATURE_CLEAN_UP)
			freecon(con);
	}
	/* /sbin/init context */
	if (getpidcon(1, &con) == 0) {
		printf(COL_FMT "%s\n", "Init context:", con);
		if (ENABLE_FEATURE_CLEAN_UP)
			freecon(con);
	}

	/* [process] context */
	for (i = 0; pc[i] != NULL; i++) {
		pidList = find_pid_by_name(bb_basename(pc[i]));
		if (pidList[0] > 0 && getpidcon(pidList[0], &con) == 0) {
			printf(COL_FMT "%s\n", pc[i], con);
			if (ENABLE_FEATURE_CLEAN_UP)
				freecon(con);
		}
		if (ENABLE_FEATURE_CLEAN_UP)
			free(pidList);
	}

	/* files contexts */
	puts("\nFile contexts:");

	cterm = xmalloc_ttyname(0);
//FIXME: if cterm == NULL, we segfault!??
	puts(cterm);
	if (cterm && lgetfilecon(cterm, &con) >= 0) {
		printf(COL_FMT "%s\n", "Controlling term:", con);
		if (ENABLE_FEATURE_CLEAN_UP)
			freecon(con);
	}

	for (i = 0; fc[i] != NULL; i++) {
		struct stat stbuf;

		if (lgetfilecon(fc[i], &con) < 0)
			continue;
		if (lstat(fc[i], &stbuf) == 0) {
			if (S_ISLNK(stbuf.st_mode)) {
				if (getfilecon(fc[i], &_con) >= 0) {
					printf(COL_FMT "%s -> %s\n", fc[i], _con, con);
					if (ENABLE_FEATURE_CLEAN_UP)
						freecon(_con);
				}
			} else {
				printf(COL_FMT "%s\n", fc[i], con);
			}
		}
		if (ENABLE_FEATURE_CLEAN_UP)
			freecon(con);
	}
}

int sestatus_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int sestatus_main(int argc UNUSED_PARAM, char **argv)
{
	unsigned opts;
	const char *pol_path;
	int rc;

	opts = getopt32(argv, "^" "vb" "\0" "=0"/*no arguments*/);

	/* SELinux status: line */
	rc = is_selinux_enabled();
	if (rc < 0)
		goto error;
	printf(COL_FMT "%s\n", "SELinux status:",
	       rc == 1 ? "enabled" : "disabled");

	/* SELinuxfs mount: line */
	if (!selinux_mnt)
		goto error;
	printf(COL_FMT "%s\n", "SELinuxfs mount:",
	       selinux_mnt);

	/* Current mode: line */
	rc = security_getenforce();
	if (rc < 0)
		goto error;
	printf(COL_FMT "%s\n", "Current mode:",
	       rc == 0 ? "permissive" : "enforcing");

	/* Mode from config file: line */
	if (selinux_getenforcemode(&rc) != 0)
		goto error;
	printf(COL_FMT "%s\n", "Mode from config file:",
	       rc < 0 ? "disabled" : (rc == 0 ? "permissive" : "enforcing"));

	/* Policy version: line */
	rc = security_policyvers();
	if (rc < 0)
		goto error;
	printf(COL_FMT "%u\n", "Policy version:", rc);

	/* Policy from config file: line */
	pol_path = selinux_policy_root();
	if (!pol_path)
		goto error;
	printf(COL_FMT "%s\n", "Policy from config file:",
	       bb_basename(pol_path));

	if (opts & OPT_BOOLEAN)
		display_boolean();
	if (opts & OPT_VERBOSE)
		display_verbose();

	return 0;

  error:
	bb_perror_msg_and_die("libselinux returns unknown state");
}
