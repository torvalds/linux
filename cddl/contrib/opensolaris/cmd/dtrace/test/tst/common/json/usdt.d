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
 * Copyright 2012, Joyent, Inc.  All rights reserved.
 */

/*
 * Sets up a fake node-bunyan-like USDT provider for use from C.
 */

provider bunyan_fake {
	probe log__trace(char *msg);
	probe log__debug(char *msg);
	probe log__info(char *msg);
	probe log__warn(char *msg);
	probe log__error(char *msg);
	probe log__fatal(char *msg);
};
