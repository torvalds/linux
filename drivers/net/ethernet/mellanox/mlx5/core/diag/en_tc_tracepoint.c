// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2019 Mellanox Technologies. */

#define CREATE_TRACE_POINTS
#include "en_tc_tracepoint.h"

void put_ids_to_array(int *ids,
		      const struct flow_action_entry *entries,
		      unsigned int num)
{
	unsigned int i;

	for (i = 0; i < num; i++)
		ids[i] = entries[i].id;
}

#define NAME_SIZE 16

static const char FLOWACT2STR[NUM_FLOW_ACTIONS][NAME_SIZE] = {
	[FLOW_ACTION_ACCEPT]	= "ACCEPT",
	[FLOW_ACTION_DROP]	= "DROP",
	[FLOW_ACTION_TRAP]	= "TRAP",
	[FLOW_ACTION_GOTO]	= "GOTO",
	[FLOW_ACTION_REDIRECT]	= "REDIRECT",
	[FLOW_ACTION_MIRRED]	= "MIRRED",
	[FLOW_ACTION_VLAN_PUSH]	= "VLAN_PUSH",
	[FLOW_ACTION_VLAN_POP]	= "VLAN_POP",
	[FLOW_ACTION_VLAN_MANGLE]	= "VLAN_MANGLE",
	[FLOW_ACTION_TUNNEL_ENCAP]	= "TUNNEL_ENCAP",
	[FLOW_ACTION_TUNNEL_DECAP]	= "TUNNEL_DECAP",
	[FLOW_ACTION_MANGLE]	= "MANGLE",
	[FLOW_ACTION_ADD]	= "ADD",
	[FLOW_ACTION_CSUM]	= "CSUM",
	[FLOW_ACTION_MARK]	= "MARK",
	[FLOW_ACTION_WAKE]	= "WAKE",
	[FLOW_ACTION_QUEUE]	= "QUEUE",
	[FLOW_ACTION_SAMPLE]	= "SAMPLE",
	[FLOW_ACTION_POLICE]	= "POLICE",
	[FLOW_ACTION_CT]	= "CT",
};

const char *parse_action(struct trace_seq *p,
			 int *ids,
			 unsigned int num)
{
	const char *ret = trace_seq_buffer_ptr(p);
	unsigned int i;

	for (i = 0; i < num; i++) {
		if (ids[i] < NUM_FLOW_ACTIONS)
			trace_seq_printf(p, "%s ", FLOWACT2STR[ids[i]]);
		else
			trace_seq_printf(p, "UNKNOWN ");
	}

	trace_seq_putc(p, 0);
	return ret;
}
