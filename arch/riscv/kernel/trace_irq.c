// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 Changbin Du <changbin.du@gmail.com>
 */

#include <linux/irqflags.h>
#include <linux/kprobes.h>
#include "trace_irq.h"

/*
 * trace_hardirqs_on/off require the caller to setup frame pointer properly.
 * Otherwise, CALLER_ADDR1 might trigger an pagging exception in kernel.
 * Here we add one extra level so they can be safely called by low
 * level entry code which $fp is used for other purpose.
 */

void __trace_hardirqs_on(void)
{
	trace_hardirqs_on();
}
NOKPROBE_SYMBOL(__trace_hardirqs_on);

void __trace_hardirqs_off(void)
{
	trace_hardirqs_off();
}
NOKPROBE_SYMBOL(__trace_hardirqs_off);
