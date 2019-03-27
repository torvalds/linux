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
 *  Test a variety of extern declarations that exercise the different
 *  kinds of declarations that we can process.
 *
 * SECTION:  Program Structure/Probe Clauses and Declarations
 *
 */

extern void *e1;
extern char e2;
extern signed char e3;
extern unsigned char e4;
extern short e5;
extern signed short e6;
extern unsigned short e7;
extern int e8;
extern e9;
extern signed int e10;
extern unsigned int e11;
extern long e12;
extern signed long e13;
extern unsigned long e14;
extern long long e15;
extern signed long long e16;
extern unsigned long long e17;
extern float e18;
extern double e19;
extern long double e20;
extern vnode_t e21;
extern struct vnode e22;
extern union sigval e23;
extern enum uio_rw e24;

BEGIN
{
	exit(0);
}
