// SPDX-License-Identifier: GPL-2.0
/*
 * Provide diagnose information via misc device /dev/diag.
 *
 * Copyright IBM Corp. 2024
 */

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/types.h>

#include <uapi/asm/diag.h>
#include "diag_ioctl.h"

static long diag_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	long rc;

	switch (cmd) {
	case DIAG324_GET_PIBLEN:
		rc = diag324_piblen(arg);
		break;
	case DIAG324_GET_PIBBUF:
		rc = diag324_pibbuf(arg);
		break;
	case DIAG310_GET_STRIDE:
		rc = diag310_memtop_stride(arg);
		break;
	case DIAG310_GET_MEMTOPLEN:
		rc = diag310_memtop_len(arg);
		break;
	case DIAG310_GET_MEMTOPBUF:
		rc = diag310_memtop_buf(arg);
		break;
	default:
		rc = -ENOIOCTLCMD;
		break;
	}
	return rc;
}

static const struct file_operations fops = {
	.owner		= THIS_MODULE,
	.open		= nonseekable_open,
	.unlocked_ioctl	= diag_ioctl,
};

static struct miscdevice diagdev = {
	.name	= "diag",
	.minor	= MISC_DYNAMIC_MINOR,
	.fops	= &fops,
	.mode	= 0444,
};

static int diag_init(void)
{
	return misc_register(&diagdev);
}

device_initcall(diag_init);
