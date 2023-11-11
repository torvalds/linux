// SPDX-License-Identifier: GPL-2.0
#include <linux/export.h>
#include "harddog.h"

#if IS_MODULE(CONFIG_UML_WATCHDOG)
EXPORT_SYMBOL(start_watchdog);
EXPORT_SYMBOL(stop_watchdog);
EXPORT_SYMBOL(ping_watchdog);
#endif
