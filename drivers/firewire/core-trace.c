// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2024 Takashi Sakamoto

#include <linux/types.h>
#include <linux/err.h>
#include "packet-header-definitions.h"
#include "phy-packet-definitions.h"

#define CREATE_TRACE_POINTS
#include <trace/events/firewire.h>

#ifdef TRACEPOINTS_ENABLED
EXPORT_TRACEPOINT_SYMBOL_GPL(isoc_inbound_single_completions);
EXPORT_TRACEPOINT_SYMBOL_GPL(isoc_inbound_multiple_completions);
EXPORT_TRACEPOINT_SYMBOL_GPL(isoc_outbound_completions);
#endif
