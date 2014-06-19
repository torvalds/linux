/*
 * Copyright (C) 2003 Christoph Hellwig.
 *	Released under GPL v2.
 */

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sysctl.h>

#include "scsi_logging.h"
#include "scsi_priv.h"


static struct ctl_table scsi_table[] = {
	{ .procname	= "logging_level",
	  .data		= &scsi_logging_level,
	  .maxlen	= sizeof(scsi_logging_level),
	  .mode		= 0644,
	  .proc_handler	= proc_dointvec },
	{ }
};

static struct ctl_table scsi_dir_table[] = {
	{ .procname	= "scsi",
	  .mode		= 0555,
	  .child	= scsi_table },
	{ }
};

static struct ctl_table scsi_root_table[] = {
	{ .procname	= "dev",
	  .mode		= 0555,
	  .child	= scsi_dir_table },
	{ }
};

static struct ctl_table_header *scsi_table_header;

int __init scsi_init_sysctl(void)
{
	scsi_table_header = register_sysctl_table(scsi_root_table);
	if (!scsi_table_header)
		return -ENOMEM;
	return 0;
}

void scsi_exit_sysctl(void)
{
	unregister_sysctl_table(scsi_table_header);
}
