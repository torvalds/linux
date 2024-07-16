/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM mlx5

#if !defined(_MLX5_CMD_TP_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _MLX5_CMD_TP_H_

#include <linux/tracepoint.h>
#include <linux/trace_seq.h>

TRACE_EVENT(mlx5_cmd,
	    TP_PROTO(const char *command_str, u16 opcode, u16 op_mod,
		     const char *status_str, u8 status, u32 syndrome, int err),
	    TP_ARGS(command_str, opcode, op_mod, status_str, status, syndrome, err),
	    TP_STRUCT__entry(__string(command_str, command_str)
			     __field(u16, opcode)
			     __field(u16, op_mod)
			    __string(status_str, status_str)
			    __field(u8, status)
			    __field(u32, syndrome)
			    __field(int, err)
			    ),
	    TP_fast_assign(__assign_str(command_str, command_str);
			__entry->opcode = opcode;
			__entry->op_mod = op_mod;
			__assign_str(status_str, status_str);
			__entry->status = status;
			__entry->syndrome = syndrome;
			__entry->err = err;
	    ),
	    TP_printk("%s(0x%x) op_mod(0x%x) failed, status %s(0x%x), syndrome (0x%x), err(%d)",
		      __get_str(command_str), __entry->opcode, __entry->op_mod,
		      __get_str(status_str), __entry->status, __entry->syndrome,
		      __entry->err)
);

#endif /* _MLX5_CMD_TP_H_ */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ./diag
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE cmd_tracepoint
#include <trace/define_trace.h>
