/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2020 NXP
 */
#ifndef _SJA1105_VL_H
#define _SJA1105_VL_H

#include "sja1105.h"

#if IS_ENABLED(CONFIG_NET_DSA_SJA1105_VL)

int sja1105_vl_redirect(struct sja1105_private *priv, int port,
			struct netlink_ext_ack *extack, unsigned long cookie,
			struct sja1105_key *key, unsigned long destports,
			bool append);

int sja1105_vl_delete(struct sja1105_private *priv, int port,
		      struct sja1105_rule *rule,
		      struct netlink_ext_ack *extack);

int sja1105_vl_gate(struct sja1105_private *priv, int port,
		    struct netlink_ext_ack *extack, unsigned long cookie,
		    struct sja1105_key *key, u32 index, s32 prio,
		    u64 base_time, u64 cycle_time, u64 cycle_time_ext,
		    u32 num_entries, struct action_gate_entry *entries);

int sja1105_vl_stats(struct sja1105_private *priv, int port,
		     struct sja1105_rule *rule, struct flow_stats *stats,
		     struct netlink_ext_ack *extack);

#else

static inline int sja1105_vl_redirect(struct sja1105_private *priv, int port,
				      struct netlink_ext_ack *extack,
				      unsigned long cookie,
				      struct sja1105_key *key,
				      unsigned long destports,
				      bool append)
{
	NL_SET_ERR_MSG_MOD(extack, "Virtual Links not compiled in");
	return -EOPNOTSUPP;
}

static inline int sja1105_vl_delete(struct sja1105_private *priv,
				    int port, struct sja1105_rule *rule,
				    struct netlink_ext_ack *extack)
{
	NL_SET_ERR_MSG_MOD(extack, "Virtual Links not compiled in");
	return -EOPNOTSUPP;
}

static inline int sja1105_vl_gate(struct sja1105_private *priv, int port,
				  struct netlink_ext_ack *extack,
				  unsigned long cookie,
				  struct sja1105_key *key, u32 index, s32 prio,
				  u64 base_time, u64 cycle_time,
				  u64 cycle_time_ext, u32 num_entries,
				  struct action_gate_entry *entries)
{
	NL_SET_ERR_MSG_MOD(extack, "Virtual Links not compiled in");
	return -EOPNOTSUPP;
}

static inline int sja1105_vl_stats(struct sja1105_private *priv, int port,
				   struct sja1105_rule *rule,
				   struct flow_stats *stats,
				   struct netlink_ext_ack *extack)
{
	NL_SET_ERR_MSG_MOD(extack, "Virtual Links not compiled in");
	return -EOPNOTSUPP;
}

#endif /* IS_ENABLED(CONFIG_NET_DSA_SJA1105_VL) */

#endif /* _SJA1105_VL_H */
