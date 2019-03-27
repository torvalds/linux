#!/bin/sh
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License, Version 1.0 only
# (the "License").  You may not use this file except in compliance
# with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#
#
# Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
#ident	"%Z%%M%	%I%	%E% SMI"

BSDECHO=-e

echo ${BSDECHO} "\
/*\n\
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.\n\
 * Use is subject to license terms.\n\
 */\n\
\n\
#pragma ident\t\"%Z%%M%\t%I%\t%E% SMI\"\n\
\n\
#include <dtrace.h>\n\
\n\
/*ARGSUSED*/
const char *\n\
dtrace_subrstr(dtrace_hdl_t *dtp, int subr)\n\
{\n\
	switch (subr) {"

nawk '
/^#define[ 	]*DIF_SUBR_/ && $2 != "DIF_SUBR_MAX" {
	printf("\tcase %s: return (\"%s\");\n", $2, tolower(substr($2, 10)));
}'

echo ${BSDECHO} "\
	default: return (\"unknown\");\n\
	}\n\
}"
