/* Copyright (C) 2007, Red Hat, Inc. */

#ifndef _LBS_CMD_H_
#define _LBS_CMD_H_

#include "hostcmd.h"
#include "dev.h"

/* lbs_cmd() infers the size of the buffer to copy data back into, from
   the size of the target of the pointer. Since the command to be sent 
   may often be smaller, that size is set in cmd->size by the caller.*/
#define lbs_cmd(priv, cmdnr, cmd, cb, cb_arg)	({		\
	uint16_t __sz = le16_to_cpu((cmd)->hdr.size);		\
	(cmd)->hdr.size = cpu_to_le16(sizeof(*(cmd)));		\
	__lbs_cmd(priv, cmdnr, &(cmd)->hdr, __sz, cb, cb_arg);	\
})

#define lbs_cmd_with_response(priv, cmdnr, cmd)	\
	lbs_cmd(priv, cmdnr, cmd, lbs_cmd_copyback, (unsigned long) (cmd))

void lbs_cmd_async(struct lbs_private *priv, uint16_t command,
	struct cmd_header *in_cmd, int in_cmd_size);

int __lbs_cmd(struct lbs_private *priv, uint16_t command,
	      struct cmd_header *in_cmd, int in_cmd_size,
	      int (*callback)(struct lbs_private *, unsigned long, struct cmd_header *),
	      unsigned long callback_arg);

int lbs_cmd_copyback(struct lbs_private *priv, unsigned long extra,
		     struct cmd_header *resp);

int lbs_update_hw_spec(struct lbs_private *priv);

int lbs_mesh_access(struct lbs_private *priv, uint16_t cmd_action,
		    struct cmd_ds_mesh_access *cmd);

int lbs_set_data_rate(struct lbs_private *priv, u8 rate);

int lbs_get_channel(struct lbs_private *priv);
int lbs_set_channel(struct lbs_private *priv, u8 channel);

int lbs_mesh_config_send(struct lbs_private *priv,
			 struct cmd_ds_mesh_config *cmd,
			 uint16_t action, uint16_t type);
int lbs_mesh_config(struct lbs_private *priv, uint16_t enable, uint16_t chan);

int lbs_host_sleep_cfg(struct lbs_private *priv, uint32_t criteria);
int lbs_suspend(struct lbs_private *priv);
void lbs_resume(struct lbs_private *priv);

int lbs_cmd_802_11_rate_adapt_rateset(struct lbs_private *priv,
				      uint16_t cmd_action);
int lbs_cmd_802_11_inactivity_timeout(struct lbs_private *priv,
				      uint16_t cmd_action, uint16_t *timeout);
int lbs_cmd_802_11_sleep_params(struct lbs_private *priv, uint16_t cmd_action,
				struct sleep_params *sp);
int lbs_cmd_802_11_set_wep(struct lbs_private *priv, uint16_t cmd_action,
			   struct assoc_request *assoc);
int lbs_cmd_802_11_enable_rsn(struct lbs_private *priv, uint16_t cmd_action,
			      uint16_t *enable);
int lbs_cmd_802_11_key_material(struct lbs_private *priv, uint16_t cmd_action,
				struct assoc_request *assoc);

#endif /* _LBS_CMD_H */
