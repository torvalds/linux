/* Copyright (C) 2007, Red Hat, Inc. */

#ifndef _LBS_CMD_H_
#define _LBS_CMD_H_

#include "hostcmd.h"
#include "dev.h"

#define lbs_cmd(priv, cmdnr, cmd, callback, callback_arg) \
	__lbs_cmd(priv, cmdnr, (struct cmd_header *) &cmd, sizeof(cmd), \
			callback, callback_arg)

int __lbs_cmd(struct lbs_private *priv, uint16_t command,
	      struct cmd_header *in_cmd, int in_cmd_size, 
	      int (*callback)(struct lbs_private *, unsigned long, struct cmd_header *),
	      unsigned long callback_arg);

#endif /* _LBS_CMD_H */
