/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright (c) 2013 Joyent, Inc.  All rights reserved.
 */

#pragma D option agghist
#pragma D option quiet

BEGIN
{
	@["demerit"] = sum(-10);
	@["wtf"] = sum(10);
	@["bot"] = sum(20);

	@bagnoogle["SOAP/XML"] = sum(1);
	@bagnoogle["XACML store"] = sum(5);
	@bagnoogle["SAML token"] = sum(6);

	@stalloogle["breakfast"] = sum(-5);
	@stalloogle["non-diet pepsi"] = sum(-20);
	@stalloogle["parrot"] = sum(-100);

	printa(@);
	printa(@bagnoogle);
	printa(@stalloogle);

	printf("\nzoomed:");

	setopt("aggzoom");
	printa(@);
	printa(@bagnoogle);
	printa(@stalloogle);

	exit(0);
}

