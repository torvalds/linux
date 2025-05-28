/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Trace events for the ChromeOS Embedded Controller
 *
 * Copyright 2025 Google LLC.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM cros_ec

#if !defined(_CROS_EC_SENSORS_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _CROS_EC_SENSORS_TRACE_H_

#include <linux/bits.h>
#include <linux/types.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>

#include <linux/tracepoint.h>

TRACE_EVENT(cros_ec_motion_host_cmd,
	    TP_PROTO(struct ec_params_motion_sense *param,
		     struct ec_response_motion_sense *resp,
		     int retval),
	    TP_ARGS(param, resp, retval),
	    TP_STRUCT__entry(__field(uint8_t, cmd)
			     __field(uint8_t, sensor_id)
			     __field(uint32_t, data)
			     __field(int, retval)
			     __field(int32_t, ret)
	    ),
	    TP_fast_assign(__entry->cmd = param->cmd;
			   __entry->sensor_id = param->sensor_odr.sensor_num;
			   __entry->data = param->sensor_odr.data;
			   __entry->retval = retval;
			   __entry->ret = retval > 0 ? resp->sensor_odr.ret : -1;
	    ),
	    TP_printk("%s, id: %d, data: %u, result: %u, return: %d",
		      __print_symbolic(__entry->cmd, MOTIONSENSE_CMDS),
		      __entry->sensor_id,
		      __entry->data,
		      __entry->retval,
		      __entry->ret)
);

#endif /* _CROS_EC_SENSORS_TRACE_H_ */

/* this part must be outside header guard */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../drivers/iio/common/cros_ec_sensors

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE cros_ec_sensors_trace

#include <trace/define_trace.h>
