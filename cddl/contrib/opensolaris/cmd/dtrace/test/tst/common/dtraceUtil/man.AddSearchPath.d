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
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * ASSERTION:
 * The -I option can be used to search path for #include files when used
 * in conjunction with the -C option. The specified directory is inserted into
 * the search path adhead of the default directory list.
 *
 * SECTION: dtrace Utility/-C Option;
 * 	dtrace Utility/-I Option
 *
 * NOTES:
 * Create a file <filename> and define the variable VALUE in it. Move it a
 * directory <dirname> different from the current directory. Change the value
 * of <filename> appropriately in the code below.
 * Invoke: dtrace -C -I <dirname> -s man.AddSearchPath.d
 * Verify VALUE.
 */


#pragma D option quiet

#include "filename"

BEGIN
{
	printf("Value of VALUE: %d\n", VALUE);
	exit(0);
}
