/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM mlx5

#if !defined(_MLX5_SF_TP_) || defined(TRACE_HEADER_MULTI_READ)
#define _MLX5_SF_TP_

#include <linux/tracepoint.h>
#include <linux/mlx5/driver.h>
#include "sf/vhca_event.h"

TRACE_EVENT(mlx5_sf_add,
	    TP_PROTO(const struct mlx5_core_dev *dev,
		     unsigned int port_index,
		     u32 controller,
		     u16 hw_fn_id,
		     u32 sfnum),
	    TP_ARGS(dev, port_index, controller, hw_fn_id, sfnum),
	    TP_STRUCT__entry(__string(devname, dev_name(dev->device))
			     __field(unsigned int, port_index)
			     __field(u32, controller)
			     __field(u16, hw_fn_id)
			     __field(u32, sfnum)
			    ),
	    TP_fast_assign(__assign_str(devname);
		    __entry->port_index = port_index;
		    __entry->controller = controller;
		    __entry->hw_fn_id = hw_fn_id;
		    __entry->sfnum = sfnum;
	    ),
	    TP_printk("(%s) port_index=%u controller=%u hw_id=0x%x sfnum=%u\n",
		      __get_str(devname), __entry->port_index, __entry->controller,
		      __entry->hw_fn_id, __entry->sfnum)
);

TRACE_EVENT(mlx5_sf_free,
	    TP_PROTO(const struct mlx5_core_dev *dev,
		     unsigned int port_index,
		     u32 controller,
		     u16 hw_fn_id),
	    TP_ARGS(dev, port_index, controller, hw_fn_id),
	    TP_STRUCT__entry(__string(devname, dev_name(dev->device))
			     __field(unsigned int, port_index)
			     __field(u32, controller)
			     __field(u16, hw_fn_id)
			    ),
	    TP_fast_assign(__assign_str(devname);
		    __entry->port_index = port_index;
		    __entry->controller = controller;
		    __entry->hw_fn_id = hw_fn_id;
	    ),
	    TP_printk("(%s) port_index=%u controller=%u hw_id=0x%x\n",
		      __get_str(devname), __entry->port_index, __entry->controller,
		      __entry->hw_fn_id)
);

TRACE_EVENT(mlx5_sf_hwc_alloc,
	    TP_PROTO(const struct mlx5_core_dev *dev,
		     u32 controller,
		     u16 hw_fn_id,
		     u32 sfnum),
	    TP_ARGS(dev, controller, hw_fn_id, sfnum),
	    TP_STRUCT__entry(__string(devname, dev_name(dev->device))
			     __field(u32, controller)
			     __field(u16, hw_fn_id)
			     __field(u32, sfnum)
			    ),
	    TP_fast_assign(__assign_str(devname);
		    __entry->controller = controller;
		    __entry->hw_fn_id = hw_fn_id;
		    __entry->sfnum = sfnum;
	    ),
	    TP_printk("(%s) controller=%u hw_id=0x%x sfnum=%u\n",
		      __get_str(devname), __entry->controller, __entry->hw_fn_id,
		      __entry->sfnum)
);

TRACE_EVENT(mlx5_sf_hwc_free,
	    TP_PROTO(const struct mlx5_core_dev *dev,
		     u16 hw_fn_id),
	    TP_ARGS(dev, hw_fn_id),
	    TP_STRUCT__entry(__string(devname, dev_name(dev->device))
			     __field(u16, hw_fn_id)
			    ),
	    TP_fast_assign(__assign_str(devname);
		    __entry->hw_fn_id = hw_fn_id;
	    ),
	    TP_printk("(%s) hw_id=0x%x\n", __get_str(devname), __entry->hw_fn_id)
);

TRACE_EVENT(mlx5_sf_hwc_deferred_free,
	    TP_PROTO(const struct mlx5_core_dev *dev,
		     u16 hw_fn_id),
	    TP_ARGS(dev, hw_fn_id),
	    TP_STRUCT__entry(__string(devname, dev_name(dev->device))
			     __field(u16, hw_fn_id)
			    ),
	    TP_fast_assign(__assign_str(devname);
		    __entry->hw_fn_id = hw_fn_id;
	    ),
	    TP_printk("(%s) hw_id=0x%x\n", __get_str(devname), __entry->hw_fn_id)
);

DECLARE_EVENT_CLASS(mlx5_sf_state_template,
		    TP_PROTO(const struct mlx5_core_dev *dev,
			     u32 port_index,
			     u32 controller,
			     u16 hw_fn_id),
		    TP_ARGS(dev, port_index, controller, hw_fn_id),
		    TP_STRUCT__entry(__string(devname, dev_name(dev->device))
				     __field(unsigned int, port_index)
				     __field(u32, controller)
				     __field(u16, hw_fn_id)),
		    TP_fast_assign(__assign_str(devname);
				   __entry->port_index = port_index;
				   __entry->controller = controller;
				   __entry->hw_fn_id = hw_fn_id;
		    ),
		    TP_printk("(%s) port_index=%u controller=%u hw_id=0x%x\n",
			      __get_str(devname), __entry->port_index, __entry->controller,
			      __entry->hw_fn_id)
);

DEFINE_EVENT(mlx5_sf_state_template, mlx5_sf_activate,
	     TP_PROTO(const struct mlx5_core_dev *dev,
		      u32 port_index,
		      u32 controller,
		      u16 hw_fn_id),
	     TP_ARGS(dev, port_index, controller, hw_fn_id)
	     );

DEFINE_EVENT(mlx5_sf_state_template, mlx5_sf_deactivate,
	     TP_PROTO(const struct mlx5_core_dev *dev,
		      u32 port_index,
		      u32 controller,
		      u16 hw_fn_id),
	     TP_ARGS(dev, port_index, controller, hw_fn_id)
	     );

TRACE_EVENT(mlx5_sf_update_state,
	    TP_PROTO(const struct mlx5_core_dev *dev,
		     unsigned int port_index,
		     u32 controller,
		     u16 hw_fn_id,
		     u8 state),
	    TP_ARGS(dev, port_index, controller, hw_fn_id, state),
	    TP_STRUCT__entry(__string(devname, dev_name(dev->device))
			     __field(unsigned int, port_index)
			     __field(u32, controller)
			     __field(u16, hw_fn_id)
			     __field(u8, state)
			    ),
	    TP_fast_assign(__assign_str(devname);
		    __entry->port_index = port_index;
		    __entry->controller = controller;
		    __entry->hw_fn_id = hw_fn_id;
		    __entry->state = state;
	    ),
	    TP_printk("(%s) port_index=%u controller=%u hw_id=0x%x state=%u\n",
		      __get_str(devname), __entry->port_index, __entry->controller,
		      __entry->hw_fn_id, __entry->state)
);

#endif /* _MLX5_SF_TP_ */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH sf/diag
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE sf_tracepoint
#include <trace/define_trace.h>
