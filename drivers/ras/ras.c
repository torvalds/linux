/*
 * Copyright (C) 2014 Intel Corporation
 *
 * Authors:
 *	Chen, Gong <gong.chen@linux.intel.com>
 */

#include <linux/init.h>
#include <linux/ras.h>

#define CREATE_TRACE_POINTS
#define TRACE_INCLUDE_PATH ../../include/ras
#include <ras/ras_event.h>

static int __init ras_init(void)
{
	int rc = 0;

	ras_debugfs_init();
	rc = ras_add_daemon_trace();

	return rc;
}
subsys_initcall(ras_init);

#if defined(CONFIG_ACPI_EXTLOG) || defined(CONFIG_ACPI_EXTLOG_MODULE)
EXPORT_TRACEPOINT_SYMBOL_GPL(extlog_mem_event);
#endif
EXPORT_TRACEPOINT_SYMBOL_GPL(mc_event);
