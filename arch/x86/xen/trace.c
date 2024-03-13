// SPDX-License-Identifier: GPL-2.0
#include <linux/ftrace.h>
#include <xen/interface/xen.h>
#include <xen/interface/xen-mca.h>

#define HYPERCALL(x)	[__HYPERVISOR_##x] = "("#x")",
static const char *xen_hypercall_names[] = {
#include <asm/xen-hypercalls.h>
};
#undef HYPERCALL

static const char *xen_hypercall_name(unsigned op)
{
	if (op < ARRAY_SIZE(xen_hypercall_names) && xen_hypercall_names[op] != NULL)
		return xen_hypercall_names[op];

	return "";
}

#define CREATE_TRACE_POINTS
#include <trace/events/xen.h>
