/*
 * Generic driver for the OLPC Embedded Controller.
 *
 * Copyright (C) 2011-2012 One Laptop per Child Foundation.
 *
 * Licensed under the GPL v2 or later.
 */
#include <linux/module.h>
#include <asm/olpc.h>

int olpc_ec_cmd(u8 cmd, u8 *inbuf, size_t inlen, u8 *outbuf, size_t outlen)
{
	/* Currently a stub; this will be expanded upon later. */
	return olpc_ec_cmd_x86(cmd, inbuf, inlen, outbuf, outlen);
}
EXPORT_SYMBOL_GPL(olpc_ec_cmd);
