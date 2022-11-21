// SPDX-License-Identifier: GPL-2.0
/*
 * fault injection support for nvme.
 *
 * Copyright (c) 2018, Oracle and/or its affiliates
 */

#include <linux/moduleparam.h>
#include "nvme.h"

static DECLARE_FAULT_ATTR(fail_default_attr);
/* optional fault injection attributes boot time option:
 * nvme_core.fail_request=<interval>,<probability>,<space>,<times>
 */
static char *fail_request;
module_param(fail_request, charp, 0000);

void nvme_fault_inject_init(struct nvme_fault_inject *fault_inj,
			    const char *dev_name)
{
	struct dentry *dir, *parent;
	struct fault_attr *attr = &fault_inj->attr;

	/* set default fault injection attribute */
	if (fail_request)
		setup_fault_attr(&fail_default_attr, fail_request);

	/* create debugfs directory and attribute */
	parent = debugfs_create_dir(dev_name, NULL);
	if (!parent) {
		pr_warn("%s: failed to create debugfs directory\n", dev_name);
		return;
	}

	*attr = fail_default_attr;
	dir = fault_create_debugfs_attr("fault_inject", parent, attr);
	if (IS_ERR(dir)) {
		pr_warn("%s: failed to create debugfs attr\n", dev_name);
		debugfs_remove_recursive(parent);
		return;
	}
	fault_inj->parent = parent;

	/* create debugfs for status code and dont_retry */
	fault_inj->status = NVME_SC_INVALID_OPCODE;
	fault_inj->dont_retry = true;
	debugfs_create_x16("status", 0600, dir,	&fault_inj->status);
	debugfs_create_bool("dont_retry", 0600, dir, &fault_inj->dont_retry);
}

void nvme_fault_inject_fini(struct nvme_fault_inject *fault_inject)
{
	/* remove debugfs directories */
	debugfs_remove_recursive(fault_inject->parent);
}

void nvme_should_fail(struct request *req)
{
	struct gendisk *disk = req->q->disk;
	struct nvme_fault_inject *fault_inject = NULL;
	u16 status;

	if (disk) {
		struct nvme_ns *ns = disk->private_data;

		if (ns)
			fault_inject = &ns->fault_inject;
		else
			WARN_ONCE(1, "No namespace found for request\n");
	} else {
		fault_inject = &nvme_req(req)->ctrl->fault_inject;
	}

	if (fault_inject && should_fail(&fault_inject->attr, 1)) {
		/* inject status code and DNR bit */
		status = fault_inject->status;
		if (fault_inject->dont_retry)
			status |= NVME_SC_DNR;
		nvme_req(req)->status =	status;
	}
}
EXPORT_SYMBOL_GPL(nvme_should_fail);
