/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM mlx5

#if !defined(_MLX5_ESW_TP_) || defined(TRACE_HEADER_MULTI_READ)
#define _MLX5_ESW_TP_

#include <linux/tracepoint.h>
#include "eswitch.h"
#include "qos.h"

TRACE_EVENT(mlx5_esw_vport_qos_destroy,
	    TP_PROTO(const struct mlx5_core_dev *dev, const struct mlx5_vport *vport),
	    TP_ARGS(dev, vport),
	    TP_STRUCT__entry(__string(devname, dev_name(dev->device))
			     __field(unsigned short, vport_id)
			     __field(unsigned int,   sched_elem_ix)
			     ),
	    TP_fast_assign(__assign_str(devname);
		    __entry->vport_id = vport->vport;
		    __entry->sched_elem_ix = mlx5_esw_qos_vport_get_sched_elem_ix(vport);
	    ),
	    TP_printk("(%s) vport=%hu sched_elem_ix=%u\n",
		      __get_str(devname), __entry->vport_id, __entry->sched_elem_ix
		      )
);

DECLARE_EVENT_CLASS(mlx5_esw_vport_qos_template,
		    TP_PROTO(const struct mlx5_core_dev *dev, const struct mlx5_vport *vport,
			     u32 bw_share, u32 max_rate),
		    TP_ARGS(dev, vport, bw_share, max_rate),
		    TP_STRUCT__entry(__string(devname, dev_name(dev->device))
				     __field(unsigned short, vport_id)
				     __field(unsigned int, sched_elem_ix)
				     __field(unsigned int, bw_share)
				     __field(unsigned int, max_rate)
				     __field(void *, parent)
				     ),
		    TP_fast_assign(__assign_str(devname);
			    __entry->vport_id = vport->vport;
			    __entry->sched_elem_ix = mlx5_esw_qos_vport_get_sched_elem_ix(vport);
			    __entry->bw_share = bw_share;
			    __entry->max_rate = max_rate;
			    __entry->parent = mlx5_esw_qos_vport_get_parent(vport);
		    ),
		    TP_printk("(%s) vport=%hu sched_elem_ix=%u bw_share=%u, max_rate=%u parent=%p\n",
			      __get_str(devname), __entry->vport_id, __entry->sched_elem_ix,
			      __entry->bw_share, __entry->max_rate, __entry->parent
			      )
);

DEFINE_EVENT(mlx5_esw_vport_qos_template, mlx5_esw_vport_qos_create,
	     TP_PROTO(const struct mlx5_core_dev *dev, const struct mlx5_vport *vport,
		      u32 bw_share, u32 max_rate),
	     TP_ARGS(dev, vport, bw_share, max_rate)
	     );

DEFINE_EVENT(mlx5_esw_vport_qos_template, mlx5_esw_vport_qos_config,
	     TP_PROTO(const struct mlx5_core_dev *dev, const struct mlx5_vport *vport,
		      u32 bw_share, u32 max_rate),
	     TP_ARGS(dev, vport, bw_share, max_rate)
	     );

DECLARE_EVENT_CLASS(mlx5_esw_node_qos_template,
		    TP_PROTO(const struct mlx5_core_dev *dev,
			     const struct mlx5_esw_sched_node *node,
			     unsigned int tsar_ix),
		    TP_ARGS(dev, node, tsar_ix),
		    TP_STRUCT__entry(__string(devname, dev_name(dev->device))
				     __field(const void *, node)
				     __field(unsigned int, tsar_ix)
				     ),
		    TP_fast_assign(__assign_str(devname);
			    __entry->node = node;
			    __entry->tsar_ix = tsar_ix;
		    ),
		    TP_printk("(%s) node=%p tsar_ix=%u\n",
			      __get_str(devname), __entry->node, __entry->tsar_ix
			      )
);

DEFINE_EVENT(mlx5_esw_node_qos_template, mlx5_esw_node_qos_create,
	     TP_PROTO(const struct mlx5_core_dev *dev,
		      const struct mlx5_esw_sched_node *node,
		      unsigned int tsar_ix),
	     TP_ARGS(dev, node, tsar_ix)
	     );

DEFINE_EVENT(mlx5_esw_node_qos_template, mlx5_esw_node_qos_destroy,
	     TP_PROTO(const struct mlx5_core_dev *dev,
		      const struct mlx5_esw_sched_node *node,
		      unsigned int tsar_ix),
	     TP_ARGS(dev, node, tsar_ix)
	     );

TRACE_EVENT(mlx5_esw_node_qos_config,
	    TP_PROTO(const struct mlx5_core_dev *dev,
		     const struct mlx5_esw_sched_node *node,
		     unsigned int tsar_ix, u32 bw_share, u32 max_rate),
	    TP_ARGS(dev, node, tsar_ix, bw_share, max_rate),
	    TP_STRUCT__entry(__string(devname, dev_name(dev->device))
			     __field(const void *, node)
			     __field(unsigned int, tsar_ix)
			     __field(unsigned int, bw_share)
			     __field(unsigned int, max_rate)
			     ),
	    TP_fast_assign(__assign_str(devname);
		    __entry->node = node;
		    __entry->tsar_ix = tsar_ix;
		    __entry->bw_share = bw_share;
		    __entry->max_rate = max_rate;
	    ),
	    TP_printk("(%s) node=%p tsar_ix=%u bw_share=%u max_rate=%u\n",
		      __get_str(devname), __entry->node, __entry->tsar_ix,
		      __entry->bw_share, __entry->max_rate
		      )
);
#endif /* _MLX5_ESW_TP_ */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH esw/diag
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE qos_tracepoint
#include <trace/define_trace.h>
