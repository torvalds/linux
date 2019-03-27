/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * String helper functions
 */

#include <string.h>
#include <sys/types.h>
#include <stdio.h>
#include <malloc.h>
#include <ctype.h>
#include "libuutil.h"

/* Return true if strings are equal */
boolean_t
uu_streq(const char *a, const char *b)
{
	return (strcmp(a, b) == 0);
}

/* Return true if strings are equal, case-insensitively */
boolean_t
uu_strcaseeq(const char *a, const char *b)
{
	return (strcasecmp(a, b) == 0);
}

/* Return true if string a Begins With string b */
boolean_t
uu_strbw(const char *a, const char *b)
{
	return (strncmp(a, b, strlen(b)) == 0);
}
