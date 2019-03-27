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
 * Copyright (c) 2012, Joyent, Inc.  All rights reserved.
 */

#pragma D option strsize=4k
#pragma D option quiet
#pragma D option destructive

/*
 * This test reads a JSON string from a USDT probe, roughly simulating the
 * primary motivating use case for the json() subroutine: filtering
 * JSON-formatted log messages from a logging subsystem like node-bunyan.
 */

pid$1:a.out:waiting:entry
{
	this->value = (int *)alloca(sizeof (int));
	*this->value = 1;
	copyout(this->value, arg0, sizeof (int));
}

bunyan*$1:::log-*
{
	this->j = copyinstr(arg0);
}

bunyan*$1:::log-*
/json(this->j, "finished") == NULL && json(this->j, "action") != "ignore"/
{
	this->index = strtoll(json(this->j, "index"));
	this->size = json(this->j, "sizes[2]");
	this->odd = json(this->j, "facts.odd");
	this->even = json(this->j, "facts.even");
	printf("[%d] sz %s odd %s even %s\n", this->index, this->size,
	    this->odd, this->even);
}

bunyan*$1:::log-*
/json(this->j, "finished") != NULL/
{
	printf("FINISHED!\n");
	exit(0);
}

tick-10s
{
	printf("ERROR: Timed out before finish message!\n");
	exit(1);
}

ERROR
{
	exit(1);
}
