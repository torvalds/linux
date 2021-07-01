// SPDX-License-Identifier: GPL-2.0
/*
 * xHCI host controller driver
 *
 * Copyright (C) 2013 Xenia Ragiadakou
 *
 * Author: Xenia Ragiadakou
 * Email : burzalodowa@gmail.com
 */

#define CREATE_TRACE_POINTS
#include "xhci-trace.h"

EXPORT_TRACEPOINT_SYMBOL_GPL(xhci_dbg_quirks);
EXPORT_TRACEPOINT_SYMBOL_GPL(xhci_urb_enqueue);
EXPORT_TRACEPOINT_SYMBOL_GPL(xhci_handle_transfer);
EXPORT_TRACEPOINT_SYMBOL_GPL(xhci_urb_giveback);
