/* Copyright (C) 2007, Red Hat, Inc. */

#ifndef _LBS_CMD_H_
#define _LBS_CMD_H_

#include "hostcmd.h"
#include "dev.h"

#define lbs_cmd(priv, cmdnr, cmd, cb, cb_arg)	\
	__lbs_cmd(priv, cmdnr, &(cmd)->hdr, sizeof(*(cmd)), cb, cb_arg)

#define lbs_cmd_with_response(priv, cmdnr, cmd)	\
	lbs_cmd(priv, cmdnr, cmd, lbs_cmd_copyback, (unsigned long) (cmd))

int __lbs_cmd(struct lbs_private *priv, uint16_t command,
	      struct cmd_header *in_cmd, int in_cmd_size,
	      int (*callback)(struct lbs_private *, unsigned long, struct cmd_header *),
	      unsigned long callback_arg);

int lbs_cmd_copyback(struct lbs_private *priv, unsigned long extra,
		     struct cmd_header *resp);

int lbs_update_hw_spec(struct lbs_private *priv);

int lbs_mesh_access(struct lbs_private *priv, uint16_t cmd_action,
		    struct cmd_ds_mesh_access *cmd);

int lbs_get_data_rate(struct lbs_private *priv);
int lbs_set_data_rate(struct lbs_private *priv, u8 rate);

int lbs_get_channel(struct lbs_private *priv);
int lbs_set_channel(struct lbs_private *priv, u8 channel);

int lbs_mesh_config(struct lbs_private *priv, uint16_t enable, uint16_t chan);

int lbs_host_sleep_cfg(struct lbs_private *priv, uint32_t criteria);
int lbs_suspend(struct lbs_private *priv);
int lbs_resume(struct lbs_private *priv);

#endif /* _LBS_CMD_H */
