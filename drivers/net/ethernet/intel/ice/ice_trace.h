/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2021 Intel Corporation. */

/* Modeled on trace-events-sample.h */

/* The trace subsystem name for ice will be "ice".
 *
 * This file is named ice_trace.h.
 *
 * Since this include file's name is different from the trace
 * subsystem name, we'll have to define TRACE_INCLUDE_FILE at the end
 * of this file.
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM ice

/* See trace-events-sample.h for a detailed description of why this
 * guard clause is different from most normal include files.
 */
#if !defined(_ICE_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _ICE_TRACE_H_

#include <linux/tracepoint.h>
#include "ice_eswitch_br.h"

/* ice_trace() macro enables shared code to refer to trace points
 * like:
 *
 * trace_ice_example(args...)
 *
 * ... as:
 *
 * ice_trace(example, args...)
 *
 * ... to resolve to the PF version of the tracepoint without
 * ifdefs, and to allow tracepoints to be disabled entirely at build
 * time.
 *
 * Trace point should always be referred to in the driver via this
 * macro.
 *
 * Similarly, ice_trace_enabled(trace_name) wraps references to
 * trace_ice_<trace_name>_enabled() functions.
 * @trace_name: name of tracepoint
 */
#define _ICE_TRACE_NAME(trace_name) (trace_##ice##_##trace_name)
#define ICE_TRACE_NAME(trace_name) _ICE_TRACE_NAME(trace_name)

#define ice_trace(trace_name, args...) ICE_TRACE_NAME(trace_name)(args)

#define ice_trace_enabled(trace_name) ICE_TRACE_NAME(trace_name##_enabled)()

/* This is for events common to PF. Corresponding versions will be named
 * trace_ice_*. The ice_trace() macro above will select the right trace point
 * name for the driver.
 */

/* Begin tracepoints */

/* Global tracepoints */

/* Events related to DIM, q_vectors and ring containers */
DECLARE_EVENT_CLASS(ice_rx_dim_template,
		    TP_PROTO(struct ice_q_vector *q_vector, struct dim *dim),
		    TP_ARGS(q_vector, dim),
		    TP_STRUCT__entry(__field(struct ice_q_vector *, q_vector)
				     __field(struct dim *, dim)
				     __string(devname, q_vector->rx.rx_ring->netdev->name)),

		    TP_fast_assign(__entry->q_vector = q_vector;
				   __entry->dim = dim;
				   __assign_str(devname, q_vector->rx.rx_ring->netdev->name);),

		    TP_printk("netdev: %s Rx-Q: %d dim-state: %d dim-profile: %d dim-tune: %d dim-st-right: %d dim-st-left: %d dim-tired: %d",
			      __get_str(devname),
			      __entry->q_vector->rx.rx_ring->q_index,
			      __entry->dim->state,
			      __entry->dim->profile_ix,
			      __entry->dim->tune_state,
			      __entry->dim->steps_right,
			      __entry->dim->steps_left,
			      __entry->dim->tired)
);

DEFINE_EVENT(ice_rx_dim_template, ice_rx_dim_work,
	     TP_PROTO(struct ice_q_vector *q_vector, struct dim *dim),
	     TP_ARGS(q_vector, dim)
);

DECLARE_EVENT_CLASS(ice_tx_dim_template,
		    TP_PROTO(struct ice_q_vector *q_vector, struct dim *dim),
		    TP_ARGS(q_vector, dim),
		    TP_STRUCT__entry(__field(struct ice_q_vector *, q_vector)
				     __field(struct dim *, dim)
				     __string(devname, q_vector->tx.tx_ring->netdev->name)),

		    TP_fast_assign(__entry->q_vector = q_vector;
				   __entry->dim = dim;
				   __assign_str(devname, q_vector->tx.tx_ring->netdev->name);),

		    TP_printk("netdev: %s Tx-Q: %d dim-state: %d dim-profile: %d dim-tune: %d dim-st-right: %d dim-st-left: %d dim-tired: %d",
			      __get_str(devname),
			      __entry->q_vector->tx.tx_ring->q_index,
			      __entry->dim->state,
			      __entry->dim->profile_ix,
			      __entry->dim->tune_state,
			      __entry->dim->steps_right,
			      __entry->dim->steps_left,
			      __entry->dim->tired)
);

DEFINE_EVENT(ice_tx_dim_template, ice_tx_dim_work,
	     TP_PROTO(struct ice_q_vector *q_vector, struct dim *dim),
	     TP_ARGS(q_vector, dim)
);

/* Events related to a vsi & ring */
DECLARE_EVENT_CLASS(ice_tx_template,
		    TP_PROTO(struct ice_tx_ring *ring, struct ice_tx_desc *desc,
			     struct ice_tx_buf *buf),

		    TP_ARGS(ring, desc, buf),
		    TP_STRUCT__entry(__field(void *, ring)
				     __field(void *, desc)
				     __field(void *, buf)
				     __string(devname, ring->netdev->name)),

		    TP_fast_assign(__entry->ring = ring;
				   __entry->desc = desc;
				   __entry->buf = buf;
				   __assign_str(devname, ring->netdev->name);),

		    TP_printk("netdev: %s ring: %pK desc: %pK buf %pK", __get_str(devname),
			      __entry->ring, __entry->desc, __entry->buf)
);

#define DEFINE_TX_TEMPLATE_OP_EVENT(name) \
DEFINE_EVENT(ice_tx_template, name, \
	     TP_PROTO(struct ice_tx_ring *ring, \
		      struct ice_tx_desc *desc, \
		      struct ice_tx_buf *buf), \
	     TP_ARGS(ring, desc, buf))

DEFINE_TX_TEMPLATE_OP_EVENT(ice_clean_tx_irq);
DEFINE_TX_TEMPLATE_OP_EVENT(ice_clean_tx_irq_unmap);
DEFINE_TX_TEMPLATE_OP_EVENT(ice_clean_tx_irq_unmap_eop);

DECLARE_EVENT_CLASS(ice_rx_template,
		    TP_PROTO(struct ice_rx_ring *ring, union ice_32b_rx_flex_desc *desc),

		    TP_ARGS(ring, desc),

		    TP_STRUCT__entry(__field(void *, ring)
				     __field(void *, desc)
				     __string(devname, ring->netdev->name)),

		    TP_fast_assign(__entry->ring = ring;
				   __entry->desc = desc;
				   __assign_str(devname, ring->netdev->name);),

		    TP_printk("netdev: %s ring: %pK desc: %pK", __get_str(devname),
			      __entry->ring, __entry->desc)
);
DEFINE_EVENT(ice_rx_template, ice_clean_rx_irq,
	     TP_PROTO(struct ice_rx_ring *ring, union ice_32b_rx_flex_desc *desc),
	     TP_ARGS(ring, desc)
);

DECLARE_EVENT_CLASS(ice_rx_indicate_template,
		    TP_PROTO(struct ice_rx_ring *ring, union ice_32b_rx_flex_desc *desc,
			     struct sk_buff *skb),

		    TP_ARGS(ring, desc, skb),

		    TP_STRUCT__entry(__field(void *, ring)
				     __field(void *, desc)
				     __field(void *, skb)
				     __string(devname, ring->netdev->name)),

		    TP_fast_assign(__entry->ring = ring;
				   __entry->desc = desc;
				   __entry->skb = skb;
				   __assign_str(devname, ring->netdev->name);),

		    TP_printk("netdev: %s ring: %pK desc: %pK skb %pK", __get_str(devname),
			      __entry->ring, __entry->desc, __entry->skb)
);

DEFINE_EVENT(ice_rx_indicate_template, ice_clean_rx_irq_indicate,
	     TP_PROTO(struct ice_rx_ring *ring, union ice_32b_rx_flex_desc *desc,
		      struct sk_buff *skb),
	     TP_ARGS(ring, desc, skb)
);

DECLARE_EVENT_CLASS(ice_xmit_template,
		    TP_PROTO(struct ice_tx_ring *ring, struct sk_buff *skb),

		    TP_ARGS(ring, skb),

		    TP_STRUCT__entry(__field(void *, ring)
				     __field(void *, skb)
				     __string(devname, ring->netdev->name)),

		    TP_fast_assign(__entry->ring = ring;
				   __entry->skb = skb;
				   __assign_str(devname, ring->netdev->name);),

		    TP_printk("netdev: %s skb: %pK ring: %pK", __get_str(devname),
			      __entry->skb, __entry->ring)
);

#define DEFINE_XMIT_TEMPLATE_OP_EVENT(name) \
DEFINE_EVENT(ice_xmit_template, name, \
	     TP_PROTO(struct ice_tx_ring *ring, struct sk_buff *skb), \
	     TP_ARGS(ring, skb))

DEFINE_XMIT_TEMPLATE_OP_EVENT(ice_xmit_frame_ring);
DEFINE_XMIT_TEMPLATE_OP_EVENT(ice_xmit_frame_ring_drop);

DECLARE_EVENT_CLASS(ice_tx_tstamp_template,
		    TP_PROTO(struct sk_buff *skb, int idx),

		    TP_ARGS(skb, idx),

		    TP_STRUCT__entry(__field(void *, skb)
				     __field(int, idx)),

		    TP_fast_assign(__entry->skb = skb;
				   __entry->idx = idx;),

		    TP_printk("skb %pK idx %d",
			      __entry->skb, __entry->idx)
);
#define DEFINE_TX_TSTAMP_OP_EVENT(name) \
DEFINE_EVENT(ice_tx_tstamp_template, name, \
	     TP_PROTO(struct sk_buff *skb, int idx), \
	     TP_ARGS(skb, idx))

DEFINE_TX_TSTAMP_OP_EVENT(ice_tx_tstamp_request);
DEFINE_TX_TSTAMP_OP_EVENT(ice_tx_tstamp_fw_req);
DEFINE_TX_TSTAMP_OP_EVENT(ice_tx_tstamp_fw_done);
DEFINE_TX_TSTAMP_OP_EVENT(ice_tx_tstamp_complete);

DECLARE_EVENT_CLASS(ice_esw_br_fdb_template,
		    TP_PROTO(struct ice_esw_br_fdb_entry *fdb),
		    TP_ARGS(fdb),
		    TP_STRUCT__entry(__array(char, dev_name, IFNAMSIZ)
				     __array(unsigned char, addr, ETH_ALEN)
				     __field(u16, vid)
				     __field(int, flags)),
		    TP_fast_assign(strscpy(__entry->dev_name,
					   netdev_name(fdb->dev),
					   IFNAMSIZ);
				   memcpy(__entry->addr, fdb->data.addr, ETH_ALEN);
				   __entry->vid = fdb->data.vid;
				   __entry->flags = fdb->flags;),
		    TP_printk("net_device=%s addr=%pM vid=%u flags=%x",
			      __entry->dev_name,
			      __entry->addr,
			      __entry->vid,
			      __entry->flags)
);

DEFINE_EVENT(ice_esw_br_fdb_template,
	     ice_eswitch_br_fdb_entry_create,
	     TP_PROTO(struct ice_esw_br_fdb_entry *fdb),
	     TP_ARGS(fdb)
);

DEFINE_EVENT(ice_esw_br_fdb_template,
	     ice_eswitch_br_fdb_entry_find_and_delete,
	     TP_PROTO(struct ice_esw_br_fdb_entry *fdb),
	     TP_ARGS(fdb)
);

DECLARE_EVENT_CLASS(ice_esw_br_vlan_template,
		    TP_PROTO(struct ice_esw_br_vlan *vlan),
		    TP_ARGS(vlan),
		    TP_STRUCT__entry(__field(u16, vid)
				     __field(u16, flags)),
		    TP_fast_assign(__entry->vid = vlan->vid;
				   __entry->flags = vlan->flags;),
		    TP_printk("vid=%u flags=%x",
			      __entry->vid,
			      __entry->flags)
);

DEFINE_EVENT(ice_esw_br_vlan_template,
	     ice_eswitch_br_vlan_create,
	     TP_PROTO(struct ice_esw_br_vlan *vlan),
	     TP_ARGS(vlan)
);

DEFINE_EVENT(ice_esw_br_vlan_template,
	     ice_eswitch_br_vlan_cleanup,
	     TP_PROTO(struct ice_esw_br_vlan *vlan),
	     TP_ARGS(vlan)
);

#define ICE_ESW_BR_PORT_NAME_L 16

DECLARE_EVENT_CLASS(ice_esw_br_port_template,
		    TP_PROTO(struct ice_esw_br_port *port),
		    TP_ARGS(port),
		    TP_STRUCT__entry(__field(u16, vport_num)
				     __array(char, port_type, ICE_ESW_BR_PORT_NAME_L)),
		    TP_fast_assign(__entry->vport_num = port->vsi_idx;
					if (port->type == ICE_ESWITCH_BR_UPLINK_PORT)
						strscpy(__entry->port_type,
							"Uplink",
							ICE_ESW_BR_PORT_NAME_L);
					else
						strscpy(__entry->port_type,
							"VF Representor",
							ICE_ESW_BR_PORT_NAME_L);),
		    TP_printk("vport_num=%u port type=%s",
			      __entry->vport_num,
			      __entry->port_type)
);

DEFINE_EVENT(ice_esw_br_port_template,
	     ice_eswitch_br_port_link,
	     TP_PROTO(struct ice_esw_br_port *port),
	     TP_ARGS(port)
);

DEFINE_EVENT(ice_esw_br_port_template,
	     ice_eswitch_br_port_unlink,
	     TP_PROTO(struct ice_esw_br_port *port),
	     TP_ARGS(port)
);

/* End tracepoints */

#endif /* _ICE_TRACE_H_ */
/* This must be outside ifdef _ICE_TRACE_H */

/* This trace include file is not located in the .../include/trace
 * with the kernel tracepoint definitions, because we're a loadable
 * module.
 */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE ../../drivers/net/ethernet/intel/ice/ice_trace
#include <trace/define_trace.h>
