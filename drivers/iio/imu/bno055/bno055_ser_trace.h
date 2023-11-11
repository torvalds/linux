/* SPDX-License-Identifier: GPL-2.0 */

#if !defined(__BNO055_SERDEV_TRACE_H__) || defined(TRACE_HEADER_MULTI_READ)
#define __BNO055_SERDEV_TRACE_H__

#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM bno055_ser

TRACE_EVENT(send_chunk,
	    TP_PROTO(int len, const u8 *data),
	    TP_ARGS(len, data),
	    TP_STRUCT__entry(
		    __field(int, len)
		    __dynamic_array(u8, chunk, len)
	    ),
	    TP_fast_assign(
		    __entry->len = len;
		    memcpy(__get_dynamic_array(chunk),
			   data, __entry->len);
	    ),
	    TP_printk("len: %d, data: = %*ph",
		      __entry->len, __entry->len, __get_dynamic_array(chunk)
	    )
);

TRACE_EVENT(cmd_retry,
	    TP_PROTO(bool read, int addr, int retry),
	    TP_ARGS(read, addr, retry),
	    TP_STRUCT__entry(
		    __field(bool, read)
		    __field(int, addr)
		    __field(int, retry)
	    ),
	    TP_fast_assign(
		    __entry->read = read;
		    __entry->addr = addr;
		    __entry->retry = retry;
	    ),
	    TP_printk("%s addr 0x%x retry #%d",
		      __entry->read ? "read" : "write",
		      __entry->addr, __entry->retry
	    )
);

TRACE_EVENT(write_reg,
	    TP_PROTO(u8 addr, u8 value),
	    TP_ARGS(addr, value),
	    TP_STRUCT__entry(
		    __field(u8, addr)
		    __field(u8, value)
	    ),
	    TP_fast_assign(
		    __entry->addr = addr;
		    __entry->value = value;
	    ),
	    TP_printk("reg 0x%x = 0x%x",
		      __entry->addr, __entry->value
	    )
);

TRACE_EVENT(read_reg,
	    TP_PROTO(int addr, size_t len),
	    TP_ARGS(addr, len),
	    TP_STRUCT__entry(
		    __field(int, addr)
		    __field(size_t, len)
	    ),
	    TP_fast_assign(
		    __entry->addr = addr;
		    __entry->len = len;
	    ),
	    TP_printk("reg 0x%x (len %zu)",
		      __entry->addr, __entry->len
	    )
);

TRACE_EVENT(recv,
	    TP_PROTO(size_t len, const unsigned char *buf),
	    TP_ARGS(len, buf),
	    TP_STRUCT__entry(
		    __field(size_t, len)
		    __dynamic_array(unsigned char, buf, len)
	    ),
	    TP_fast_assign(
		    __entry->len = len;
		    memcpy(__get_dynamic_array(buf),
			   buf, __entry->len);
	    ),
	    TP_printk("len: %zu, data: = %*ph",
		      __entry->len, (int)__entry->len, __get_dynamic_array(buf)
	    )
);

#endif /* __BNO055_SERDEV_TRACE_H__ || TRACE_HEADER_MULTI_READ */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE bno055_ser_trace

/* This part must be outside protection */
#include <trace/define_trace.h>
