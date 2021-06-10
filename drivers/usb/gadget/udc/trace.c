// SPDX-License-Identifier: GPL-2.0
/**
 * trace.c - USB Gadget Framework Trace Support
 *
 * Copyright (C) 2016 Intel Corporation
 * Author: Felipe Balbi <felipe.balbi@linux.intel.com>
 */

#define CREATE_TRACE_POINTS
#include "trace.h"

EXPORT_TRACEPOINT_SYMBOL_GPL(usb_gadget_connect);
EXPORT_TRACEPOINT_SYMBOL_GPL(usb_gadget_disconnect);
