/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __CRIS_STACKTRACE_H
#define __CRIS_STACKTRACE_H

void walk_stackframe(unsigned long sp,
		     int (*fn)(unsigned long addr, void *data),
		     void *data);

#endif
