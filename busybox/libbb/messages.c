/* vi: set sw=4 ts=4: */
/*
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"

/* allow version to be extended, via CFLAGS */
#ifndef BB_EXTRA_VERSION
#define BB_EXTRA_VERSION " ("AUTOCONF_TIMESTAMP")"
#endif

const char bb_banner[] ALIGN1 = "BusyBox v" BB_VER BB_EXTRA_VERSION;


const char bb_msg_memory_exhausted[] ALIGN1 = "out of memory";
const char bb_msg_invalid_date[] ALIGN1 = "invalid date '%s'";
const char bb_msg_unknown[] ALIGN1 = "(unknown)";
const char bb_msg_can_not_create_raw_socket[] ALIGN1 = "can't create raw socket";
const char bb_msg_perm_denied_are_you_root[] ALIGN1 = "permission denied (are you root?)";
const char bb_msg_you_must_be_root[] ALIGN1 = "you must be root";
const char bb_msg_requires_arg[] ALIGN1 = "%s requires an argument";
const char bb_msg_invalid_arg_to[] ALIGN1 = "invalid argument '%s' to '%s'";
const char bb_msg_standard_input[] ALIGN1 = "standard input";
const char bb_msg_standard_output[] ALIGN1 = "standard output";

const char bb_hexdigits_upcase[] ALIGN1 = "0123456789ABCDEF";

const char bb_busybox_exec_path[] ALIGN1 = CONFIG_BUSYBOX_EXEC_PATH;
const char bb_default_login_shell[] ALIGN1 = LIBBB_DEFAULT_LOGIN_SHELL;
/* util-linux manpage says /sbin:/bin:/usr/sbin:/usr/bin,
 * but I want to save a few bytes here. Check libbb.h before changing! */
const char bb_PATH_root_path[] ALIGN1 = BB_PATH_ROOT_PATH;


//const int const_int_1 = 1;
/* explicitly = 0, otherwise gcc may make it a common variable
 * and it will end up in bss */
const int const_int_0 = 0;

#if ENABLE_FEATURE_WTMP
/* This is usually something like "/var/adm/wtmp" or "/var/log/wtmp" */
const char bb_path_wtmp_file[] ALIGN1 =
# if defined _PATH_WTMP
	_PATH_WTMP;
# elif defined WTMP_FILE
	WTMP_FILE;
# else
#  error unknown path to wtmp file
# endif
#endif
