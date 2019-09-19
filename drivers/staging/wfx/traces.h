/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Tracepoints definitions.
 *
 * Copyright (c) 2018-2019, Silicon Laboratories, Inc.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM wfx

#if !defined(_WFX_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _WFX_TRACE_H

#include <linux/tracepoint.h>

#include "bus.h"

/* The hell below need some explanations. For each symbolic number, we need to
 * define it with TRACE_DEFINE_ENUM() and in a list for __print_symbolic.
 *
 *   1. Define a new macro that call TRACE_DEFINE_ENUM():
 *
 *          #define xxx_name(sym) TRACE_DEFINE_ENUM(sym);
 *
 *   2. Define list of all symbols:
 *
 *          #define list_names     \
 *             ...                 \
 *             xxx_name(XXX)       \
 *             ...
 *
 *   3. Instanciate that list_names:
 *
 *          list_names
 *
 *   4. Redefine xxx_name() as a entry of array for __print_symbolic()
 *
 *          #undef xxx_name
 *          #define xxx_name(msg) { msg, #msg },
 *
 *   5. list_name can now nearlu be used with __print_symbolic() but,
 *      __print_symbolic() dislike last comma of list. So we define a new list
 *      with a dummy element:
 *
 *          #define list_for_print_symbolic list_names { -1, NULL }
 */

#define wfx_reg_list_enum                                 \
	wfx_reg_name(WFX_REG_CONFIG,       "CONFIG")      \
	wfx_reg_name(WFX_REG_CONTROL,      "CONTROL")     \
	wfx_reg_name(WFX_REG_IN_OUT_QUEUE, "QUEUE")       \
	wfx_reg_name(WFX_REG_AHB_DPORT,    "AHB")         \
	wfx_reg_name(WFX_REG_BASE_ADDR,    "BASE_ADDR")   \
	wfx_reg_name(WFX_REG_SRAM_DPORT,   "SRAM")        \
	wfx_reg_name(WFX_REG_SET_GEN_R_W,  "SET_GEN_R_W") \
	wfx_reg_name(WFX_REG_FRAME_OUT,    "FRAME_OUT")

#undef wfx_reg_name
#define wfx_reg_name(sym, name) TRACE_DEFINE_ENUM(sym);
wfx_reg_list_enum
#undef wfx_reg_name
#define wfx_reg_name(sym, name) { sym, name },
#define wfx_reg_list wfx_reg_list_enum { -1, NULL }

DECLARE_EVENT_CLASS(io_data,
	TP_PROTO(int reg, int addr, const void *io_buf, size_t len),
	TP_ARGS(reg, addr, io_buf, len),
	TP_STRUCT__entry(
		__field(int, reg)
		__field(int, addr)
		__field(int, msg_len)
		__field(int, buf_len)
		__array(u8, buf, 32)
		__array(u8, addr_str, 10)
	),
	TP_fast_assign(
		__entry->reg = reg;
		__entry->addr = addr;
		__entry->msg_len = len;
		__entry->buf_len = min_t(int, sizeof(__entry->buf), __entry->msg_len);
		memcpy(__entry->buf, io_buf, __entry->buf_len);
		if (addr >= 0)
			snprintf(__entry->addr_str, 10, "/%08x", addr);
		else
			__entry->addr_str[0] = 0;
	),
	TP_printk("%s%s: %s%s (%d bytes)",
		__print_symbolic(__entry->reg, wfx_reg_list),
		__entry->addr_str,
		__print_hex(__entry->buf, __entry->buf_len),
		__entry->msg_len > sizeof(__entry->buf) ? " ..." : "",
		__entry->msg_len
	)
);
DEFINE_EVENT(io_data, io_write,
	TP_PROTO(int reg, int addr, const void *io_buf, size_t len),
	TP_ARGS(reg, addr, io_buf, len));
#define _trace_io_ind_write(reg, addr, io_buf, len) trace_io_write(reg, addr, io_buf, len)
#define _trace_io_write(reg, io_buf, len) trace_io_write(reg, -1, io_buf, len)
DEFINE_EVENT(io_data, io_read,
	TP_PROTO(int reg, int addr, const void *io_buf, size_t len),
	TP_ARGS(reg, addr, io_buf, len));
#define _trace_io_ind_read(reg, addr, io_buf, len) trace_io_read(reg, addr, io_buf, len)
#define _trace_io_read(reg, io_buf, len) trace_io_read(reg, -1, io_buf, len)

DECLARE_EVENT_CLASS(io_data32,
	TP_PROTO(int reg, int addr, u32 val),
	TP_ARGS(reg, addr, val),
	TP_STRUCT__entry(
		__field(int, reg)
		__field(int, addr)
		__field(int, val)
		__array(u8, addr_str, 10)
	),
	TP_fast_assign(
		__entry->reg = reg;
		__entry->addr = addr;
		__entry->val = val;
		if (addr >= 0)
			snprintf(__entry->addr_str, 10, "/%08x", addr);
		else
			__entry->addr_str[0] = 0;
	),
	TP_printk("%s%s: %08x",
		__print_symbolic(__entry->reg, wfx_reg_list),
		__entry->addr_str,
		__entry->val
	)
);
DEFINE_EVENT(io_data32, io_write32,
	TP_PROTO(int reg, int addr, u32 val),
	TP_ARGS(reg, addr, val));
#define _trace_io_ind_write32(reg, addr, val) trace_io_write32(reg, addr, val)
#define _trace_io_write32(reg, val) trace_io_write32(reg, -1, val)
DEFINE_EVENT(io_data32, io_read32,
	TP_PROTO(int reg, int addr, u32 val),
	TP_ARGS(reg, addr, val));
#define _trace_io_ind_read32(reg, addr, val) trace_io_read32(reg, addr, val)
#define _trace_io_read32(reg, val) trace_io_read32(reg, -1, val)

#endif

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE traces

#include <trace/define_trace.h>
