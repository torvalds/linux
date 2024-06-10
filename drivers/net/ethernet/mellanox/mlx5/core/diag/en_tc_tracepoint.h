/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019 Mellanox Technologies. */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM mlx5

#if !defined(_MLX5_TC_TP_) || defined(TRACE_HEADER_MULTI_READ)
#define _MLX5_TC_TP_

#include <linux/tracepoint.h>
#include <linux/trace_seq.h>
#include <net/flow_offload.h>
#include "en_rep.h"

#define __parse_action(ids, num) parse_action(p, ids, num)

void put_ids_to_array(int *ids,
		      const struct flow_action_entry *entries,
		      unsigned int num);

const char *parse_action(struct trace_seq *p,
			 int *ids,
			 unsigned int num);

DECLARE_EVENT_CLASS(mlx5e_flower_template,
		    TP_PROTO(const struct flow_cls_offload *f),
		    TP_ARGS(f),
		    TP_STRUCT__entry(__field(void *, cookie)
				     __field(unsigned int, num)
				     __dynamic_array(int, ids, f->rule ?
					     f->rule->action.num_entries : 0)
				     ),
		    TP_fast_assign(__entry->cookie = (void *)f->cookie;
			__entry->num = (f->rule ?
				f->rule->action.num_entries : 0);
			if (__entry->num)
				put_ids_to_array(__get_dynamic_array(ids),
						 f->rule->action.entries,
						 f->rule->action.num_entries);
			),
		    TP_printk("cookie=%p actions= %s\n",
			      __entry->cookie, __entry->num ?
				      __parse_action(__get_dynamic_array(ids),
						     __entry->num) : "NULL"
			      )
);

DEFINE_EVENT(mlx5e_flower_template, mlx5e_configure_flower,
	     TP_PROTO(const struct flow_cls_offload *f),
	     TP_ARGS(f)
	     );

DEFINE_EVENT(mlx5e_flower_template, mlx5e_delete_flower,
	     TP_PROTO(const struct flow_cls_offload *f),
	     TP_ARGS(f)
	     );

TRACE_EVENT(mlx5e_stats_flower,
	    TP_PROTO(const struct flow_cls_offload *f),
	    TP_ARGS(f),
	    TP_STRUCT__entry(__field(void *, cookie)
			     __field(u64, bytes)
			     __field(u64, packets)
			     __field(u64, lastused)
			     ),
	    TP_fast_assign(__entry->cookie = (void *)f->cookie;
		__entry->bytes = f->stats.bytes;
		__entry->packets = f->stats.pkts;
		__entry->lastused = f->stats.lastused;
		),
	    TP_printk("cookie=%p bytes=%llu packets=%llu lastused=%llu\n",
		      __entry->cookie, __entry->bytes,
		      __entry->packets, __entry->lastused
		      )
);

TRACE_EVENT(mlx5e_tc_update_neigh_used_value,
	    TP_PROTO(const struct mlx5e_neigh_hash_entry *nhe, bool neigh_used),
	    TP_ARGS(nhe, neigh_used),
	    TP_STRUCT__entry(__string(devname, nhe->neigh_dev->name)
			     __array(u8, v4, 4)
			     __array(u8, v6, 16)
			     __field(bool, neigh_used)
			     ),
	    TP_fast_assign(const struct mlx5e_neigh *mn = &nhe->m_neigh;
			struct in6_addr *pin6;
			__be32 *p32;

			__assign_str(devname);
			__entry->neigh_used = neigh_used;

			p32 = (__be32 *)__entry->v4;
			pin6 = (struct in6_addr *)__entry->v6;
			if (mn->family == AF_INET) {
				*p32 = mn->dst_ip.v4;
				ipv6_addr_set_v4mapped(*p32, pin6);
			} else if (mn->family == AF_INET6) {
				*pin6 = mn->dst_ip.v6;
			}
			),
	    TP_printk("netdev: %s IPv4: %pI4 IPv6: %pI6c neigh_used=%d\n",
		      __get_str(devname), __entry->v4, __entry->v6,
		      __entry->neigh_used
		      )
);

#endif /* _MLX5_TC_TP_ */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ./diag
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE en_tc_tracepoint
#include <trace/define_trace.h>
