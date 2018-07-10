/*
 * chcon -- change security context, based on coreutils-5.97-13
 *
 * Port to busybox: KaiGai Kohei <kaigai@kaigai.gr.jp>
 *
 * Copyright (C) 2006 - 2007 KaiGai Kohei <kaigai@kaigai.gr.jp>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config CHCON
//config:	bool "chcon (8.9 kb)"
//config:	default n
//config:	depends on SELINUX
//config:	help
//config:	Enable support to change the security context of file.

//applet:IF_CHCON(APPLET(chcon, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_CHCON) += chcon.o

//usage:#define chcon_trivial_usage
//usage:       "[OPTIONS] CONTEXT FILE..."
//usage:       "\n	chcon [OPTIONS] [-u USER] [-r ROLE] [-l RANGE] [-t TYPE] FILE..."
//usage:	IF_LONG_OPTS(
//usage:       "\n	chcon [OPTIONS] --reference=RFILE FILE..."
//usage:	)
//usage:
//usage:#define chcon_full_usage "\n\n"
//usage:       "Change the security context of each FILE to CONTEXT\n"
//usage:     "\n	-v	Verbose"
//usage:     "\n	-c	Report changes made"
//usage:     "\n	-h	Affect symlinks instead of their targets"
//usage:     "\n	-f	Suppress most error messages"
//usage:	IF_LONG_OPTS(
//usage:     "\n	--reference RFILE Use RFILE's group instead of using a CONTEXT value"
//usage:	)
//usage:     "\n	-u USER	Set user/role/type/range in the target security context"
//usage:     "\n	-r ROLE"
//usage:     "\n	-t TYPE"
//usage:     "\n	-l RNG"
//usage:     "\n	-R	Recurse"

#include <selinux/context.h>

#include "libbb.h"

#define OPT_RECURSIVE		(1<<0)	/* 'R' */
#define OPT_CHANHES		(1<<1)	/* 'c' */
#define OPT_NODEREFERENCE	(1<<2)	/* 'h' */
#define OPT_QUIET		(1<<3)	/* 'f' */
#define OPT_USER		(1<<4)	/* 'u' */
#define OPT_ROLE		(1<<5)	/* 'r' */
#define OPT_TYPE		(1<<6)	/* 't' */
#define OPT_RANGE		(1<<7)	/* 'l' */
#define OPT_VERBOSE		(1<<8)	/* 'v' */
#define OPT_REFERENCE		((1<<9) * ENABLE_LONG_OPTS)
#define OPT_COMPONENT_SPECIFIED	(OPT_USER | OPT_ROLE | OPT_TYPE | OPT_RANGE)

static char *user = NULL;
static char *role = NULL;
static char *type = NULL;
static char *range = NULL;
static char *specified_context = NULL;

static int FAST_FUNC change_filedir_context(
		const char *fname,
		struct stat *stbuf UNUSED_PARAM,
		void *userData UNUSED_PARAM,
		int depth UNUSED_PARAM)
{
	context_t context = NULL;
	security_context_t file_context = NULL;
	security_context_t context_string;
	int rc = FALSE;
	int status = 0;

	if (option_mask32 & OPT_NODEREFERENCE) {
		status = lgetfilecon(fname, &file_context);
	} else {
		status = getfilecon(fname, &file_context);
	}
	if (status < 0 && errno != ENODATA) {
		if ((option_mask32 & OPT_QUIET) == 0)
			bb_error_msg("can't obtain security context: %s", fname);
		goto skip;
	}

	if (file_context == NULL && specified_context == NULL) {
		bb_error_msg("can't apply partial context to unlabeled file %s", fname);
		goto skip;
	}

	if (specified_context == NULL) {
		context = set_security_context_component(file_context,
							user, role, type, range);
		if (!context) {
			bb_error_msg("can't compute security context from %s", file_context);
			goto skip;
		}
	} else {
		context = context_new(specified_context);
		if (!context) {
			bb_error_msg("invalid context: %s", specified_context);
			goto skip;
		}
	}

	context_string = context_str(context);
	if (!context_string) {
		bb_error_msg("can't obtain security context in text expression");
		goto skip;
	}

	if (file_context == NULL || strcmp(context_string, file_context) != 0) {
		int fail;

		if (option_mask32 & OPT_NODEREFERENCE) {
			fail = lsetfilecon(fname, context_string);
		} else {
			fail = setfilecon(fname, context_string);
		}
		if ((option_mask32 & OPT_VERBOSE) || ((option_mask32 & OPT_CHANHES) && !fail)) {
			printf(!fail
				? "context of %s changed to %s\n"
				: "can't change context of %s to %s\n",
				fname, context_string);
		}
		if (!fail) {
			rc = TRUE;
		} else if ((option_mask32 & OPT_QUIET) == 0) {
			bb_error_msg("can't change context of %s to %s",
					fname, context_string);
		}
	} else if (option_mask32 & OPT_VERBOSE) {
		printf("context of %s retained as %s\n", fname, context_string);
		rc = TRUE;
	}
skip:
	context_free(context);
	freecon(file_context);

	return rc;
}

#if ENABLE_LONG_OPTS
static const char chcon_longopts[] ALIGN1 =
	"recursive\0"      No_argument       "R"
	"changes\0"        No_argument       "c"
	"no-dereference\0" No_argument       "h"
	"silent\0"         No_argument       "f"
	"quiet\0"          No_argument       "f"
	"user\0"           Required_argument "u"
	"role\0"           Required_argument "r"
	"type\0"           Required_argument "t"
	"range\0"          Required_argument "l"
	"verbose\0"        No_argument       "v"
	"reference\0"      Required_argument "\xff" /* no short option */
	;
#endif

int chcon_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int chcon_main(int argc UNUSED_PARAM, char **argv)
{
	char *reference_file;
	char *fname;
	int i, errors = 0;

	getopt32long(argv, "^"
		"Rchfu:r:t:l:v"
		"\0"
		"-1" /* at least 1 arg */
		":?" /* error if exclusivity constraints are violated */
#if ENABLE_LONG_OPTS
		":\xff--urtl:u--\xff:r--\xff:t--\xff:l--\xff"
#endif
		":f--v:v--f"  /* 'verbose' and 'quiet' are exclusive */
		, chcon_longopts,
		&user, &role, &type, &range, &reference_file
	);
	argv += optind;

#if ENABLE_LONG_OPTS
	if (option_mask32 & OPT_REFERENCE) {
		/* FIXME: lgetfilecon() should be used when '-h' is specified.
		 * But current implementation follows the original one. */
		if (getfilecon(reference_file, &specified_context) < 0)
			bb_perror_msg_and_die("getfilecon('%s') failed", reference_file);
	} else
#endif
	if ((option_mask32 & OPT_COMPONENT_SPECIFIED) == 0) {
		specified_context = *argv++;
		/* specified_context is never NULL -
		 * "-1" in opt_complementary prevents this. */
		if (!argv[0])
			bb_error_msg_and_die("too few arguments");
	}

	for (i = 0; (fname = argv[i]) != NULL; i++) {
		int fname_len = strlen(fname);
		while (fname_len > 1 && fname[fname_len - 1] == '/')
			fname_len--;
		fname[fname_len] = '\0';

		if (recursive_action(fname,
					1<<option_mask32 & OPT_RECURSIVE,
					change_filedir_context,
					change_filedir_context,
					NULL, 0) != TRUE)
			errors = 1;
	}
	return errors;
}
