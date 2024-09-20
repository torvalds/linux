/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-or-later */
/*
 * Copyright 2013-2015 Freescale Semiconductor Inc.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM	dpaa_eth

#if !defined(_DPAA_ETH_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _DPAA_ETH_TRACE_H

#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include "dpaa_eth.h"
#include <linux/tracepoint.h>

#define fd_format_name(format)	{ qm_fd_##format, #format }
#define fd_format_list	\
	fd_format_name(contig),	\
	fd_format_name(sg)

/* This is used to declare a class of events.
 * individual events of this type will be defined below.
 */

/* Store details about a frame descriptor and the FQ on which it was
 * transmitted/received.
 */
DECLARE_EVENT_CLASS(dpaa_eth_fd,
	/* Trace function prototype */
	TP_PROTO(struct net_device *netdev,
		 struct qman_fq *fq,
		 const struct qm_fd *fd),

	/* Repeat argument list here */
	TP_ARGS(netdev, fq, fd),

	/* A structure containing the relevant information we want to record.
	 * Declare name and type for each normal element, name, type and size
	 * for arrays. Use __string for variable length strings.
	 */
	TP_STRUCT__entry(
		__field(u32,	fqid)
		__field(u64,	fd_addr)
		__field(u8,	fd_format)
		__field(u16,	fd_offset)
		__field(u32,	fd_length)
		__field(u32,	fd_status)
		__string(name,	netdev->name)
	),

	/* The function that assigns values to the above declared fields */
	TP_fast_assign(
		__entry->fqid = fq->fqid;
		__entry->fd_addr = qm_fd_addr_get64(fd);
		__entry->fd_format = qm_fd_get_format(fd);
		__entry->fd_offset = qm_fd_get_offset(fd);
		__entry->fd_length = qm_fd_get_length(fd);
		__entry->fd_status = fd->status;
		__assign_str(name);
	),

	/* This is what gets printed when the trace event is triggered */
	TP_printk("[%s] fqid=%d, fd: addr=0x%llx, format=%s, off=%u, len=%u, status=0x%08x",
		  __get_str(name), __entry->fqid, __entry->fd_addr,
		  __print_symbolic(__entry->fd_format, fd_format_list),
		  __entry->fd_offset, __entry->fd_length, __entry->fd_status)
);

/* Now declare events of the above type. Format is:
 * DEFINE_EVENT(class, name, proto, args), with proto and args same as for class
 */

/* Tx (egress) fd */
DEFINE_EVENT(dpaa_eth_fd, dpaa_tx_fd,

	TP_PROTO(struct net_device *netdev,
		 struct qman_fq *fq,
		 const struct qm_fd *fd),

	TP_ARGS(netdev, fq, fd)
);

/* Rx fd */
DEFINE_EVENT(dpaa_eth_fd, dpaa_rx_fd,

	TP_PROTO(struct net_device *netdev,
		 struct qman_fq *fq,
		 const struct qm_fd *fd),

	TP_ARGS(netdev, fq, fd)
);

/* Tx confirmation fd */
DEFINE_EVENT(dpaa_eth_fd, dpaa_tx_conf_fd,

	TP_PROTO(struct net_device *netdev,
		 struct qman_fq *fq,
		 const struct qm_fd *fd),

	TP_ARGS(netdev, fq, fd)
);

/* If only one event of a certain type needs to be declared, use TRACE_EVENT().
 * The syntax is the same as for DECLARE_EVENT_CLASS().
 */

#endif /* _DPAA_ETH_TRACE_H */

/* This must be outside ifdef _DPAA_ETH_TRACE_H */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE	dpaa_eth_trace
#include <trace/define_trace.h>
