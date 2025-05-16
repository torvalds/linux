/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2007, Red Hat, Inc. */

#ifndef _LBS_CMD_H_
#define _LBS_CMD_H_

#include <net/cfg80211.h>

#include "host.h"
#include "dev.h"


/* Command & response transfer between host and card */

struct cmd_ctrl_node {
	struct list_head list;
	int result;
	/* command response */
	int (*callback)(struct lbs_private *,
			unsigned long,
			struct cmd_header *);
	unsigned long callback_arg;
	/* command data */
	struct cmd_header *cmdbuf;
	/* wait queue */
	u16 cmdwaitqwoken;
	wait_queue_head_t cmdwait_q;
};


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

struct cmd_ctrl_node *__lbs_cmd_async(struct lbs_private *priv,
	uint16_t command, struct cmd_header *in_cmd, int in_cmd_size,
	int (*callback)(struct lbs_private *, unsigned long, struct cmd_header *),
	unsigned long callback_arg);

int lbs_cmd_copyback(struct lbs_private *priv, unsigned long extra,
		     struct cmd_header *resp);

int lbs_allocate_cmd_buffer(struct lbs_private *priv);
int lbs_free_cmd_buffer(struct lbs_private *priv);

int lbs_execute_next_command(struct lbs_private *priv);
void __lbs_complete_command(struct lbs_private *priv, struct cmd_ctrl_node *cmd,
			    int result);
void lbs_complete_command(struct lbs_private *priv, struct cmd_ctrl_node *cmd,
			  int result);
int lbs_process_command_response(struct lbs_private *priv, u8 *data, u32 len);


/* From cmdresp.c */

void lbs_mac_event_disconnected(struct lbs_private *priv,
				bool locally_generated);



/* Events */

void lbs_process_event(struct lbs_private *priv, u32 event);


/* Actual commands */

int lbs_update_hw_spec(struct lbs_private *priv);

int lbs_set_channel(struct lbs_private *priv, u8 channel);

int lbs_update_channel(struct lbs_private *priv);

int lbs_host_sleep_cfg(struct lbs_private *priv, uint32_t criteria,
		struct wol_config *p_wol_config);

int lbs_cmd_802_11_sleep_params(struct lbs_private *priv, uint16_t cmd_action,
				struct sleep_params *sp);

void lbs_ps_confirm_sleep(struct lbs_private *priv);

int lbs_set_radio(struct lbs_private *priv, u8 preamble, u8 radio_on);

void lbs_set_mac_control(struct lbs_private *priv);
int lbs_set_mac_control_sync(struct lbs_private *priv);

int lbs_get_tx_power(struct lbs_private *priv, s16 *curlevel, s16 *minlevel,
		     s16 *maxlevel);

int lbs_set_snmp_mib(struct lbs_private *priv, u32 oid, u16 val);


/* Commands only used in wext.c, assoc. and scan.c */

int lbs_set_deep_sleep(struct lbs_private *priv, int deep_sleep);

int lbs_set_host_sleep(struct lbs_private *priv, int host_sleep);

int lbs_set_monitor_mode(struct lbs_private *priv, int enable);

int lbs_get_rssi(struct lbs_private *priv, s8 *snr, s8 *nf);

int lbs_set_11d_domain_info(struct lbs_private *priv);

int lbs_get_reg(struct lbs_private *priv, u16 reg, u16 offset, u32 *value);

int lbs_set_reg(struct lbs_private *priv, u16 reg, u16 offset, u32 value);

int lbs_set_ps_mode(struct lbs_private *priv, u16 cmd_action, bool block);

#endif /* _LBS_CMD_H */
