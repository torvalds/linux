/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM mlx5

#if !defined(_MLX5_SF_DEV_TP_) || defined(TRACE_HEADER_MULTI_READ)
#define _MLX5_SF_DEV_TP_

#include <linux/tracepoint.h>
#include <linux/mlx5/driver.h>
#include "../../dev/dev.h"

DECLARE_EVENT_CLASS(mlx5_sf_dev_template,
		    TP_PROTO(const struct mlx5_core_dev *dev,
			     const struct mlx5_sf_dev *sfdev,
			     int aux_id),
		    TP_ARGS(dev, sfdev, aux_id),
		    TP_STRUCT__entry(__string(devname, dev_name(dev->device))
				     __field(const struct mlx5_sf_dev*, sfdev)
				     __field(int, aux_id)
				     __field(u16, hw_fn_id)
				     __field(u32, sfnum)
		    ),
		    TP_fast_assign(__assign_str(devname, dev_name(dev->device));
				   __entry->sfdev = sfdev;
				   __entry->aux_id = aux_id;
				   __entry->hw_fn_id = sfdev->fn_id;
				   __entry->sfnum = sfdev->sfnum;
		    ),
		    TP_printk("(%s) sfdev=%pK aux_id=%d hw_id=0x%x sfnum=%u\n",
			      __get_str(devname), __entry->sfdev,
			      __entry->aux_id, __entry->hw_fn_id,
			      __entry->sfnum)
);

DEFINE_EVENT(mlx5_sf_dev_template, mlx5_sf_dev_add,
	     TP_PROTO(const struct mlx5_core_dev *dev,
		      const struct mlx5_sf_dev *sfdev,
		      int aux_id),
	     TP_ARGS(dev, sfdev, aux_id)
	     );

DEFINE_EVENT(mlx5_sf_dev_template, mlx5_sf_dev_del,
	     TP_PROTO(const struct mlx5_core_dev *dev,
		      const struct mlx5_sf_dev *sfdev,
		      int aux_id),
	     TP_ARGS(dev, sfdev, aux_id)
	     );

#endif /* _MLX5_SF_DEV_TP_ */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH sf/dev/diag
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE dev_tracepoint
#include <trace/define_trace.h>
