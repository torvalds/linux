/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM mlx5

#if !defined(_MLX5_SF_VHCA_TP_) || defined(TRACE_HEADER_MULTI_READ)
#define _MLX5_SF_VHCA_TP_

#include <linux/tracepoint.h>
#include <linux/mlx5/driver.h>
#include "sf/vhca_event.h"

TRACE_EVENT(mlx5_sf_vhca_event,
	    TP_PROTO(const struct mlx5_core_dev *dev,
		     const struct mlx5_vhca_state_event *event),
	    TP_ARGS(dev, event),
	    TP_STRUCT__entry(__string(devname, dev_name(dev->device))
			     __field(u16, hw_fn_id)
			     __field(u32, sfnum)
			     __field(u8, vhca_state)
			    ),
	    TP_fast_assign(__assign_str(devname);
		    __entry->hw_fn_id = event->function_id;
		    __entry->sfnum = event->sw_function_id;
		    __entry->vhca_state = event->new_vhca_state;
	    ),
	    TP_printk("(%s) hw_id=0x%x sfnum=%u vhca_state=%d\n",
		      __get_str(devname), __entry->hw_fn_id,
		      __entry->sfnum, __entry->vhca_state)
);

#endif /* _MLX5_SF_VHCA_TP_ */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH sf/diag
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE vhca_tracepoint
#include <trace/define_trace.h>
