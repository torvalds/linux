/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019 Mellanox Technologies. */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM mlx5

#if !defined(_MLX5_EN_REP_TP_) || defined(TRACE_HEADER_MULTI_READ)
#define _MLX5_EN_REP_TP_

#include <linux/tracepoint.h>
#include <linux/trace_seq.h>
#include "en_rep.h"

TRACE_EVENT(mlx5e_rep_neigh_update,
	    TP_PROTO(const struct mlx5e_neigh_hash_entry *nhe, const u8 *ha,
		     bool neigh_connected),
	    TP_ARGS(nhe, ha, neigh_connected),
	    TP_STRUCT__entry(__string(devname, nhe->neigh_dev->name)
			     __array(u8, ha, ETH_ALEN)
			     __array(u8, v4, 4)
			     __array(u8, v6, 16)
			     __field(bool, neigh_connected)
			     ),
	    TP_fast_assign(const struct mlx5e_neigh *mn = &nhe->m_neigh;
			struct in6_addr *pin6;
			__be32 *p32;

			__assign_str(devname, nhe->neigh_dev->name);
			__entry->neigh_connected = neigh_connected;
			memcpy(__entry->ha, ha, ETH_ALEN);

			p32 = (__be32 *)__entry->v4;
			pin6 = (struct in6_addr *)__entry->v6;
			if (mn->family == AF_INET) {
				*p32 = mn->dst_ip.v4;
				ipv6_addr_set_v4mapped(*p32, pin6);
			} else if (mn->family == AF_INET6) {
				*pin6 = mn->dst_ip.v6;
			}
			),
	    TP_printk("netdev: %s MAC: %pM IPv4: %pI4 IPv6: %pI6c neigh_connected=%d\n",
		      __get_str(devname), __entry->ha,
		      __entry->v4, __entry->v6, __entry->neigh_connected
		      )
);

#endif /* _MLX5_EN_REP_TP_ */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ./diag
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE en_rep_tracepoint
#include <trace/define_trace.h>
