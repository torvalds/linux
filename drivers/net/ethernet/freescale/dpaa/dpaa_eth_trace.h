/* Copyright 2013-2015 Freescale Semiconductor Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *	 notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *	 notice, this list of conditions and the following disclaimer in the
 *	 documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *	 names of its contributors may be used to endorse or promote products
 *	 derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
		__assign_str(name, netdev->name);
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
