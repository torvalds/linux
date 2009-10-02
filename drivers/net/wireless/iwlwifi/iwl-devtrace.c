#include <linux/module.h>

/* sparse doesn't like tracepoint macros */
#ifndef __CHECKER__
#define CREATE_TRACE_POINTS
#include "iwl-devtrace.h"

EXPORT_TRACEPOINT_SYMBOL(iwlwifi_dev_ioread32);
EXPORT_TRACEPOINT_SYMBOL(iwlwifi_dev_iowrite32);
EXPORT_TRACEPOINT_SYMBOL(iwlwifi_dev_rx);
EXPORT_TRACEPOINT_SYMBOL(iwlwifi_dev_ucode_event);
EXPORT_TRACEPOINT_SYMBOL(iwlwifi_dev_ucode_error);
#endif
