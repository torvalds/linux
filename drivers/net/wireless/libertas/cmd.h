/* Copyright (C) 2007, Red Hat, Inc. */

#ifndef _LBS_CMD_H_
#define _LBS_CMD_H_

#include "hostcmd.h"
#include "dev.h"

#define lbs_cmd(priv, cmdnr, cmd, callback, callback_arg) \
	__lbs_cmd(priv, cmdnr, &(cmd).hdr, sizeof(cmd),	  \
			callback, callback_arg)

#define lbs_cmd_with_response(priv, cmdnr, cmd) \
	__lbs_cmd(priv, cmdnr, &(cmd).hdr, sizeof(cmd), \
		  lbs_cmd_copyback, (unsigned long) &cmd)
 
int __lbs_cmd(struct lbs_private *priv, uint16_t command,
	      struct cmd_header *in_cmd, int in_cmd_size, 
	      int (*callback)(struct lbs_private *, unsigned long, struct cmd_header *),
	      unsigned long callback_arg);

int lbs_cmd_copyback(struct lbs_private *priv, unsigned long extra,
		     struct cmd_header *resp);

int lbs_update_hw_spec(struct lbs_private *priv);

int lbs_mesh_access(struct lbs_private *priv, uint16_t cmd_action,
		    struct cmd_ds_mesh_access *cmd);

#endif /* _LBS_CMD_H */
