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
# Copyright 2003 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
#ident	"%Z%%M%	%I%	%E% SMI"

BSDECHO=-e

echo ${BSDECHO} "\
/*\n\
 * Copyright 2003 Sun Microsystems, Inc.  All rights reserved.\n\
 * Use is subject to license terms.\n\
 */\n\
\n\
#pragma ident\t\"%Z%%M%\t%I%\t%E% SMI\"\n\
\n\
#include <dt_errtags.h>
\n\
static const char *const _dt_errtags[] = {"

pattern='^	\(D_[A-Z0-9_]*\),*'
replace='	"\1",'

sed -n "s/$pattern/$replace/p" || exit 1

echo ${BSDECHO} "\
};\n\
\n\
static const int _dt_ntag = sizeof (_dt_errtags) / sizeof (_dt_errtags[0]);\n\
\n\
const char *
dt_errtag(dt_errtag_t tag)
{
	return (_dt_errtags[(tag > 0 && tag < _dt_ntag) ? tag : 0]);
}"

exit 0
