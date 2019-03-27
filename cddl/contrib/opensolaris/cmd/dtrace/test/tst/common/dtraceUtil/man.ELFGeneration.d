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
 * Using -G option with dtrace utility produces an ELF file containing a
 * DTrace program. If the filename used with the -s option does ends
 * with .d, and the -o option is not used, then the output ELF file is
 * in filename.o
 *
 * SECTION: dtrace Utility/-G Option
 *
 * NOTES: Use this file as
 * /usr/sbin/dtrace -G -s man.ELFGeneration.d
 * Delete the file man.ELFGeneration.d.o
 *
 */

BEGIN
{
	printf("This test should compile.\n");
	exit(0);
}
