// SPDX-License-Identifier: GPL-2.0
/*
 * Tracepoint definitions for vfio_ccw
 *
 * Copyright IBM Corp. 2019
 * Author(s): Eric Farman <farman@linux.ibm.com>
 */

#define CREATE_TRACE_POINTS
#include "vfio_ccw_trace.h"

EXPORT_TRACEPOINT_SYMBOL(vfio_ccw_chp_event);
EXPORT_TRACEPOINT_SYMBOL(vfio_ccw_fsm_async_request);
EXPORT_TRACEPOINT_SYMBOL(vfio_ccw_fsm_event);
EXPORT_TRACEPOINT_SYMBOL(vfio_ccw_fsm_io_request);
