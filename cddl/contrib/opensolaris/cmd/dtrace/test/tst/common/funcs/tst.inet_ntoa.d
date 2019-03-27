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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#pragma D option quiet

in_addr_t *ip4a;
in_addr_t *ip4b;
in_addr_t *ip4c;
in_addr_t *ip4d;

BEGIN
{
	this->buf4a = alloca(sizeof (in_addr_t));
	this->buf4b = alloca(sizeof (in_addr_t));
	this->buf4c = alloca(sizeof (in_addr_t));
	this->buf4d = alloca(sizeof (in_addr_t));
	ip4a = this->buf4a;
	ip4b = this->buf4b;
	ip4c = this->buf4c;
	ip4d = this->buf4d;

	*ip4a = htonl(0xc0a80117);
	*ip4b = htonl(0x7f000001);
	*ip4c = htonl(0xffffffff);
	*ip4d = htonl(0x00000000);

	printf("%s\n", inet_ntoa(ip4a));
	printf("%s\n", inet_ntoa(ip4b));
	printf("%s\n", inet_ntoa(ip4c));
	printf("%s\n", inet_ntoa(ip4d));

	exit(0);
}
