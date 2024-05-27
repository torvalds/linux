// SPDX-License-Identifier: GPL-2.0
/*
 *   CIO inject interface
 *
 *    Copyright IBM Corp. 2021
 *    Author(s): Vineeth Vijayan <vneethv@linux.ibm.com>
 */

#define KMSG_COMPONENT "cio"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/debugfs.h>
#include <asm/chpid.h>

#include "cio_inject.h"
#include "cio_debug.h"

static DEFINE_SPINLOCK(crw_inject_lock);
DEFINE_STATIC_KEY_FALSE(cio_inject_enabled);
static struct crw *crw_inject_data;

/**
 * crw_inject : Initiate the artificial CRW inject
 * @crw: The data which needs to be injected as new CRW.
 *
 * The CRW handler is called, which will use the provided artificial
 * data instead of the CRW from the underlying hardware.
 *
 * Return: 0 on success
 */
static int crw_inject(struct crw *crw)
{
	int rc = 0;
	struct crw *copy;
	unsigned long flags;

	copy = kmemdup(crw, sizeof(*crw), GFP_KERNEL);
	if (!copy)
		return -ENOMEM;

	spin_lock_irqsave(&crw_inject_lock, flags);
	if (crw_inject_data) {
		kfree(copy);
		rc = -EBUSY;
	} else {
		crw_inject_data = copy;
	}
	spin_unlock_irqrestore(&crw_inject_lock, flags);

	if (!rc)
		crw_handle_channel_report();

	return rc;
}

/**
 * stcrw_get_injected: Copy the artificial CRW data to CRW struct.
 * @crw: The target CRW pointer.
 *
 * Retrieve an injected CRW data. Return 0 on success, 1 if no
 * injected-CRW is available. The function reproduces the return
 * code of the actual STCRW function.
 */
int stcrw_get_injected(struct crw *crw)
{
	int rc = 1;
	unsigned long flags;

	spin_lock_irqsave(&crw_inject_lock, flags);
	if (crw_inject_data) {
		memcpy(crw, crw_inject_data, sizeof(*crw));
		kfree(crw_inject_data);
		crw_inject_data = NULL;
		rc = 0;
	}
	spin_unlock_irqrestore(&crw_inject_lock, flags);

	return rc;
}

/* The debugfs write handler for crw_inject nodes operation */
static ssize_t crw_inject_write(struct file *file, const char __user *buf,
				size_t lbuf, loff_t *ppos)
{
	u32 slct, oflw, chn, rsc, anc, erc, rsid;
	struct crw crw;
	char *buffer;
	int rc;

	if (!static_branch_likely(&cio_inject_enabled)) {
		pr_warn("CIO inject is not enabled - ignoring CRW inject\n");
		return -EINVAL;
	}

	buffer = memdup_user_nul(buf, lbuf);
	if (IS_ERR(buffer))
		return -ENOMEM;

	rc = sscanf(buffer, "%x %x %x %x %x %x %x", &slct, &oflw, &chn, &rsc, &anc,
		    &erc, &rsid);

	kvfree(buffer);
	if (rc != 7) {
		pr_warn("crw_inject: Invalid format (need <solicited> <overflow> <chaining> <rsc> <ancillary> <erc> <rsid>)\n");
		return -EINVAL;
	}

	memset(&crw, 0, sizeof(crw));
	crw.slct = slct;
	crw.oflw = oflw;
	crw.chn = chn;
	crw.rsc = rsc;
	crw.anc = anc;
	crw.erc = erc;
	crw.rsid = rsid;

	rc = crw_inject(&crw);
	if (rc)
		return rc;

	return lbuf;
}

/* Debugfs write handler for inject_enable node*/
static ssize_t enable_inject_write(struct file *file, const char __user *buf,
				   size_t lbuf, loff_t *ppos)
{
	unsigned long en = 0;
	int rc;

	rc = kstrtoul_from_user(buf, lbuf, 10, &en);
	if (rc)
		return rc;

	switch (en) {
	case 0:
		static_branch_disable(&cio_inject_enabled);
		break;
	case 1:
		static_branch_enable(&cio_inject_enabled);
		break;
	}

	return lbuf;
}

static const struct file_operations crw_fops = {
	.owner = THIS_MODULE,
	.write = crw_inject_write,
};

static const struct file_operations cio_en_fops = {
	.owner = THIS_MODULE,
	.write = enable_inject_write,
};

static int __init cio_inject_init(void)
{
	/* enable_inject node enables the static branching */
	debugfs_create_file("enable_inject", 0200, cio_debugfs_dir,
			    NULL, &cio_en_fops);

	debugfs_create_file("crw_inject", 0200, cio_debugfs_dir,
			    NULL, &crw_fops);
	return 0;
}

device_initcall(cio_inject_init);
